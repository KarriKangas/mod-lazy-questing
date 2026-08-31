#!/usr/bin/env python3
"""Aggregate one Lazy Questing flight-recorder run from Playerbots.log.

Raw runtime logs stay outside the repository. This script filters by the exact run ID,
pairs scheduler/flight/gear records, rejects incomplete or arm-count-mismatched intervals,
and derives rates from the activity seconds actually observed in each arm.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MODES = ("control", "assist-only")
XP_FIELDS = ("xp", "kill_xp", "quest_xp", "explore_xp", "other_xp")
ACTIVITY_FIELDS = ("travel_s", "fight_s", "loot_s", "interact_s", "service_s", "dead_s", "idle_s")
EVENT_FIELDS = (
    "kills",
    "deaths",
    "loot_events",
    "quest_pickups",
    "objective_deltas",
    "quest_completions",
    "quest_turn_ins",
    "level_ups",
)
GEAR_EVENT_FIELDS = ("loot_items", "loot_gear", "quest_gear", "equip_events")
GEAR_SUM_FIELDS = ("loot_gear_ilvl_sum", "quest_gear_ilvl_sum", "equip_ilvl_sum")
TOTAL_FIELDS = (*XP_FIELDS, *ACTIVITY_FIELDS, *EVENT_FIELDS, *GEAR_EVENT_FIELDS, *GEAR_SUM_FIELDS)

SCHEDULER_RE = re.compile(
    r"roster/registered/pending=(\d+)/(\d+)/(\d+).*?"
    r"modes control/assist/current=(\d+)/(\d+)/(\d+).*?"
    r"progress/repoints/preemptions=(\d+)/(\d+)/(\d+).*?"
    r"exhausted/hard-stalls=(\d+)/(\d+).*?"
    r"budget-limited ticks=(\d+)"
)
FLIGHT_RE = re.compile(
    r"mode=(control|assist-only|current) bots=(\d+) "
    r"xp total/kill/quest/explore/other=([\d/]+), "
    r"activity-s travel/fight/loot/interact/service/dead/idle=([\d/]+), "
    r"events kills/deaths/loot/pickups/objective-deltas/completions/turn-ins/levels=([\d/]+), "
    r"intents=(\d+)"
)
GEAR_RE = re.compile(
    r"mode=(control|assist-only|current) bots=(\d+) samples=(\d+) "
    r"snapshot level/avg-ilvl/total-ilvl/slots/weapon-ilvl=([\d./]+), "
    r"items loot/loot-gear/quest-gear/equips=([\d/]+), "
    r"item-level-sums loot-gear/quest-gear/equips=([\d/]+)"
)
INTENT_RE = re.compile(
    r"mode=(control|assist-only|current) type=([\w-]+) "
    r"acquired/progress/repoints/preemptions=([\d/]+), "
    r"endings success/protected/lease/exhausted/hard/cancelled=([\d/]+), "
    r"distance avg/max=([\d.]+)/([\d.]+), duration-avg=([\d.]+)s, "
    r"source none/null/grind/explore/rpg/quest/other=([\d/]+)"
)


def split_ints(value: str, expected: int) -> list[int]:
    result = [int(part) for part in value.split("/")]
    if len(result) != expected:
        raise ValueError(f"expected {expected} integer fields in {value!r}")
    return result


def split_floats(value: str, expected: int) -> list[float]:
    result = [float(part) for part in value.split("/")]
    if len(result) != expected:
        raise ValueError(f"expected {expected} floating fields in {value!r}")
    return result


@dataclass
class Interval:
    scheduler: dict[str, int] | None = None
    flight: dict[str, dict[str, float]] = field(default_factory=dict)
    gear: dict[str, dict[str, float]] = field(default_factory=dict)
    intents: list[dict[str, Any]] = field(default_factory=list)
    first_line: int = 0
    last_line: int = 0


def parse_log(path: Path, run_id: str) -> list[Interval]:
    marker = f"run={run_id}"
    intervals: list[Interval] = []
    current: Interval | None = None

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, 1):
            if marker not in line:
                continue

            if "[LQ] scheduler" in line:
                if current is not None:
                    intervals.append(current)
                current = Interval(first_line=line_number, last_line=line_number)
                match = SCHEDULER_RE.search(line)
                if match:
                    values = [int(value) for value in match.groups()]
                    keys = (
                        "roster",
                        "registered",
                        "pending",
                        "control",
                        "assist_only",
                        "current",
                        "progress",
                        "repoints",
                        "preemptions",
                        "exhausted",
                        "hard_stalls",
                        "budget_limited_ticks",
                    )
                    current.scheduler = dict(zip(keys, values))
                continue

            if current is None:
                continue
            current.last_line = line_number

            if "[LQ][flight]" in line:
                match = FLIGHT_RE.search(line)
                if not match:
                    continue
                mode, bots, xp_values, activity_values, event_values, intents = match.groups()
                record: dict[str, float] = {"bots": int(bots), "active_intents": int(intents)}
                record.update(zip(XP_FIELDS, split_ints(xp_values, len(XP_FIELDS))))
                record.update(zip(ACTIVITY_FIELDS, split_ints(activity_values, len(ACTIVITY_FIELDS))))
                record.update(zip(EVENT_FIELDS, split_ints(event_values, len(EVENT_FIELDS))))
                current.flight[mode] = record
            elif "[LQ][gear]" in line:
                match = GEAR_RE.search(line)
                if not match:
                    continue
                mode, bots, samples, snapshot_values, event_values, sum_values = match.groups()
                record = {"bots": int(bots), "samples": int(samples)}
                record.update(
                    zip(
                        ("level", "average_item_level", "total_item_level", "occupied_slots", "weapon_item_level"),
                        split_floats(snapshot_values, 5),
                    )
                )
                record.update(zip(GEAR_EVENT_FIELDS, split_ints(event_values, len(GEAR_EVENT_FIELDS))))
                record.update(zip(GEAR_SUM_FIELDS, split_ints(sum_values, len(GEAR_SUM_FIELDS))))
                current.gear[mode] = record
            elif "[LQ][intent]" in line:
                match = INTENT_RE.search(line)
                if not match:
                    continue
                mode, intent_type, acquired, endings, distance_avg, distance_max, duration_avg, sources = match.groups()
                record = {"mode": mode, "type": intent_type}
                record.update(zip(("acquired", "progress", "repoints", "preemptions"), split_ints(acquired, 4)))
                record.update(
                    zip(
                        ("success", "protected", "lease", "exhausted", "hard", "cancelled"),
                        split_ints(endings, 6),
                    )
                )
                record.update(
                    {
                        "distance_avg": float(distance_avg),
                        "distance_max": float(distance_max),
                        "duration_avg": float(duration_avg),
                    }
                )
                record.update(
                    zip(
                        ("source_none", "source_null", "source_grind", "source_explore", "source_rpg", "source_quest", "source_other"),
                        split_ints(sources, 7),
                    )
                )
                current.intents.append(record)

    if current is not None:
        intervals.append(current)
    return intervals


def select_intervals(
    intervals: list[Interval],
    expected_bots: int,
    expected_samples: int | None,
    exclude_first: int,
    limit: int | None,
) -> tuple[list[Interval], list[dict[str, Any]]]:
    valid: list[Interval] = []
    exclusions: list[dict[str, Any]] = []
    for index, interval in enumerate(intervals, 1):
        reasons: list[str] = []
        if interval.scheduler is None:
            reasons.append("unparseable scheduler")
        else:
            if interval.scheduler["roster"] != expected_bots * len(MODES):
                reasons.append(f"scheduler roster={interval.scheduler['roster']}")
            if interval.scheduler["control"] != expected_bots:
                reasons.append(f"scheduler control={interval.scheduler['control']}")
            if interval.scheduler["assist_only"] != expected_bots:
                reasons.append(f"scheduler assist-only={interval.scheduler['assist_only']}")
            if interval.scheduler["current"] != 0:
                reasons.append(f"scheduler current={interval.scheduler['current']}")
        for mode in MODES:
            if mode not in interval.flight:
                reasons.append(f"missing {mode} flight")
            elif interval.flight[mode]["bots"] != expected_bots:
                reasons.append(f"{mode} flight bots={int(interval.flight[mode]['bots'])}")
            elif interval.flight[mode]["xp"] != sum(
                interval.flight[mode][key]
                for key in ("kill_xp", "quest_xp", "explore_xp", "other_xp")
            ):
                reasons.append(f"{mode} XP components do not sum to total")
            if mode not in interval.gear:
                reasons.append(f"missing {mode} gear")
            elif interval.gear[mode]["bots"] != expected_bots:
                reasons.append(f"{mode} gear bots={int(interval.gear[mode]['bots'])}")
            elif expected_samples is not None and interval.gear[mode]["samples"] != expected_samples:
                reasons.append(f"{mode} gear samples={int(interval.gear[mode]['samples'])}")
        if reasons:
            exclusions.append({"interval": index, "lines": [interval.first_line, interval.last_line], "reasons": reasons})
        else:
            valid.append(interval)

    for _ in range(min(exclude_first, len(valid))):
        interval = valid.pop(0)
        exclusions.append(
            {
                "interval": intervals.index(interval) + 1,
                "lines": [interval.first_line, interval.last_line],
                "reasons": ["predeclared startup fragment"],
            }
        )

    if limit is not None and len(valid) > limit:
        valid = valid[:limit]
    return valid, sorted(exclusions, key=lambda item: item["interval"])


def relative(treatment: float | None, control: float | None) -> float | None:
    if treatment is None or control in (None, 0):
        return None
    return (treatment / control - 1.0) * 100.0


def aggregate_arm(intervals: list[Interval], mode: str) -> dict[str, Any]:
    totals: defaultdict[str, float] = defaultdict(float)
    weighted_snapshots: defaultdict[str, float] = defaultdict(float)
    total_samples = 0.0
    for interval in intervals:
        flight = interval.flight[mode]
        gear = interval.gear[mode]
        for key in (*XP_FIELDS, *ACTIVITY_FIELDS, *EVENT_FIELDS):
            totals[key] += flight[key]
        for key in (*GEAR_EVENT_FIELDS, *GEAR_SUM_FIELDS):
            totals[key] += gear[key]
        samples = gear["samples"]
        total_samples += samples
        for key in ("level", "average_item_level", "total_item_level", "occupied_slots", "weapon_item_level"):
            weighted_snapshots[key] += gear[key] * samples

    bot_seconds = sum(totals[key] for key in ACTIVITY_FIELDS)
    bot_hours = bot_seconds / 3600.0
    rates = {
        key: (totals[key] / bot_hours if bot_hours else 0.0)
        for key in (*XP_FIELDS, *EVENT_FIELDS, *GEAR_EVENT_FIELDS)
    }
    activity = {key: (totals[key] / bot_seconds * 100.0 if bot_seconds else 0.0) for key in ACTIVITY_FIELDS}
    snapshot = {
        key: (weighted_snapshots[key] / total_samples if total_samples else 0.0)
        for key in ("level", "average_item_level", "total_item_level", "occupied_slots", "weapon_item_level")
    }
    item_quality = {
        "loot_gear_average_item_level": (
            totals["loot_gear_ilvl_sum"] / totals["loot_gear"] if totals["loot_gear"] else None
        ),
        "quest_gear_average_item_level": (
            totals["quest_gear_ilvl_sum"] / totals["quest_gear"] if totals["quest_gear"] else None
        ),
        "equip_event_average_item_level": (
            totals["equip_ilvl_sum"] / totals["equip_events"] if totals["equip_events"] else None
        ),
    }
    return {
        "intervals": len(intervals),
        "bot_seconds": bot_seconds,
        "bot_hours": bot_hours,
        "totals": dict(totals),
        "rates": rates,
        "activity_percent": activity,
        "weighted_snapshot": snapshot,
        "item_quality": item_quality,
        "last_snapshot": intervals[-1].gear[mode] if intervals else {},
        "loot_per_kill": totals["loot_events"] / totals["kills"] if totals["kills"] else None,
    }


def add_comparisons(result: dict[str, Any]) -> dict[str, Any]:
    comparisons: dict[str, float | None] = {}
    control = result["arms"]["control"]
    treatment = result["arms"]["assist-only"]
    for key in (*XP_FIELDS, *EVENT_FIELDS, *GEAR_EVENT_FIELDS):
        comparisons[f"{key}_rate_relative_percent"] = relative(treatment["rates"][key], control["rates"][key])
    for key in ACTIVITY_FIELDS:
        comparisons[f"{key}_percentage_point_difference"] = (
            treatment["activity_percent"][key] - control["activity_percent"][key]
        )
    comparisons["loot_per_kill_relative_percent"] = relative(treatment["loot_per_kill"], control["loot_per_kill"])
    for key in (
        "loot_gear_average_item_level",
        "quest_gear_average_item_level",
        "equip_event_average_item_level",
    ):
        comparisons[f"{key}_relative_percent"] = relative(
            treatment["item_quality"][key], control["item_quality"][key]
        )
    result["comparisons"] = comparisons
    return result


def aggregate(intervals: list[Interval]) -> dict[str, Any]:
    return add_comparisons(
        {
            "intervals": len(intervals),
            "arms": {mode: aggregate_arm(intervals, mode) for mode in MODES},
        }
    )


def combine_arm_aggregate(prior: dict[str, Any], current: dict[str, Any]) -> dict[str, Any]:
    """Combine additive arm totals from a captured checkpoint and one parsed epoch.

    A checkpoint may explicitly use null for a field that was not preserved before its
    raw log was lost. Such a field remains null instead of silently treating it as zero.
    Snapshot averages are not additive, so the combined result exposes only the final
    snapshot from the current epoch.
    """

    prior_totals = prior["totals"]
    current_totals = current["totals"]
    totals: dict[str, float | None] = {}
    for key in TOTAL_FIELDS:
        prior_value = prior_totals.get(key)
        current_value = current_totals.get(key)
        totals[key] = None if prior_value is None or current_value is None else prior_value + current_value

    bot_seconds = sum(float(totals[key] or 0.0) for key in ACTIVITY_FIELDS)
    bot_hours = bot_seconds / 3600.0
    rates = {
        key: (float(totals[key]) / bot_hours if totals[key] is not None and bot_hours else None)
        for key in (*XP_FIELDS, *EVENT_FIELDS, *ASSIST_FIELDS, *GEAR_EVENT_FIELDS)
    }
    activity = {
        key: (float(totals[key]) / bot_seconds * 100.0 if bot_seconds else 0.0)
        for key in ACTIVITY_FIELDS
    }

    def average(sum_key: str, count_key: str) -> float | None:
        total_sum = totals[sum_key]
        count = totals[count_key]
        if total_sum is None or count in (None, 0):
            return None
        return float(total_sum) / float(count)

    kills = totals["kills"]
    loot_events = totals["loot_events"]
    return {
        "intervals": int(prior["intervals"]) + int(current["intervals"]),
        "bot_seconds": bot_seconds,
        "bot_hours": bot_hours,
        "totals": totals,
        "rates": rates,
        "activity_percent": activity,
        "weighted_snapshot": None,
        "item_quality": {
            "loot_gear_average_item_level": average("loot_gear_ilvl_sum", "loot_gear"),
            "quest_gear_average_item_level": average("quest_gear_ilvl_sum", "quest_gear"),
            "equip_event_average_item_level": average("equip_ilvl_sum", "equip_events"),
        },
        "last_snapshot": current["last_snapshot"],
        "loot_per_kill": (
            float(loot_events) / float(kills)
            if loot_events is not None and kills not in (None, 0)
            else None
        ),
    }


def combine_with_prior(prior_path: Path, run_id: str, current: dict[str, Any]) -> dict[str, Any]:
    with prior_path.open("r", encoding="utf-8") as handle:
        prior = json.load(handle)
    if prior.get("run_id") != run_id:
        raise ValueError(
            f"prior aggregate run ID {prior.get('run_id')!r} does not match requested run ID {run_id!r}"
        )
    for mode in MODES:
        arm = prior["arms"][mode]
        if int(arm["intervals"]) != int(prior["retained_intervals"]):
            raise ValueError(f"prior {mode} interval count does not match retained_intervals")
        totals = arm["totals"]
        activity_sum = sum(float(totals[key]) for key in ACTIVITY_FIELDS)
        if not math.isclose(activity_sum, float(arm["bot_seconds"]), rel_tol=0.0, abs_tol=0.001):
            raise ValueError(f"prior {mode} activity fields do not sum to bot_seconds")
        component_xp = sum(float(totals[key]) for key in ("kill_xp", "quest_xp", "explore_xp", "other_xp"))
        if not math.isclose(component_xp, float(totals["xp"]), rel_tol=0.0, abs_tol=0.001):
            raise ValueError(f"prior {mode} XP components do not sum to total XP")
    combined = {
        "intervals": int(prior["retained_intervals"]) + int(current["intervals"]),
        "arms": {
            mode: combine_arm_aggregate(prior["arms"][mode], current["arms"][mode])
            for mode in MODES
        },
        "sources": {
            "prior_checkpoint": str(prior_path),
            "prior_intervals": int(prior["retained_intervals"]),
            "current_epoch_intervals": int(current["intervals"]),
            "snapshot_policy": "final current-epoch snapshot only; snapshot averages are not additive",
        },
    }
    return add_comparisons(combined)


def aggregate_level_bands(intervals: list[Interval]) -> dict[str, Any]:
    if not intervals:
        return {}
    minimum = math.floor(min(interval.gear[mode]["level"] for interval in intervals for mode in MODES))
    maximum = math.floor(max(interval.gear[mode]["level"] for interval in intervals for mode in MODES))
    result: dict[str, Any] = {}
    for level in range(minimum, maximum + 1):
        by_mode = {
            mode: [interval for interval in intervals if level <= interval.gear[mode]["level"] < level + 1]
            for mode in MODES
        }
        if not all(by_mode.values()):
            continue
        result[str(level)] = add_comparisons(
            {
                "level_range": [level, level + 1],
                "arms": {mode: aggregate_arm(by_mode[mode], mode) for mode in MODES},
            }
        )
    return result


def aggregate_intents(intervals: list[Interval]) -> dict[str, Any]:
    totals: defaultdict[str, float] = defaultdict(float)
    acquired_weight = 0.0
    ended_weight = 0.0
    for interval in intervals:
        for record in interval.intents:
            prefix = f"{record['mode']}:{record['type']}"
            for key in (
                "acquired",
                "progress",
                "repoints",
                "preemptions",
                "success",
                "protected",
                "lease",
                "exhausted",
                "hard",
                "cancelled",
                "source_none",
                "source_null",
                "source_grind",
                "source_explore",
                "source_rpg",
                "source_quest",
                "source_other",
            ):
                totals[f"{prefix}:{key}"] += record[key]
            totals[f"{prefix}:distance_weighted"] += record["distance_avg"] * record["acquired"]
            acquired_weight += record["acquired"]
            endings = sum(record[key] for key in ("success", "protected", "lease", "exhausted", "hard", "cancelled"))
            totals[f"{prefix}:duration_weighted"] += record["duration_avg"] * endings
            ended_weight += endings
            totals[f"{prefix}:distance_max"] = max(totals[f"{prefix}:distance_max"], record["distance_max"])
    return {
        "totals": dict(totals),
        "acquired": acquired_weight,
        "ended": ended_weight,
    }


def aggregate_scheduler(intervals: list[Interval]) -> dict[str, int]:
    keys = ("progress", "repoints", "preemptions", "exhausted", "hard_stalls", "budget_limited_ticks")
    return {key: sum((interval.scheduler or {}).get(key, 0) for interval in intervals) for key in keys}


def summarize_exposure_integrity(intervals: list[Interval], expected_bots: int) -> dict[str, Any]:
    sample_counts = {
        mode: Counter(int(interval.gear[mode]["samples"]) for interval in intervals)
        for mode in MODES
    }
    activity_seconds = {
        mode: [sum(float(interval.flight[mode][key]) for key in ACTIVITY_FIELDS) for interval in intervals]
        for mode in MODES
    }
    control_active_intents = [
        {
            "lines": [interval.first_line, interval.last_line],
            "active_intents": int(interval.flight["control"]["active_intents"]),
        }
        for interval in intervals
        if interval.flight["control"]["active_intents"]
    ]
    return {
        "retained_intervals": len(intervals),
        "expected_bots_per_arm": expected_bots,
        "all_scheduler_counts_expected": all(
            interval.scheduler is not None
            and interval.scheduler["roster"] == expected_bots * len(MODES)
            and interval.scheduler["control"] == expected_bots
            and interval.scheduler["assist_only"] == expected_bots
            and interval.scheduler["current"] == 0
            for interval in intervals
        ),
        "all_flight_and_gear_counts_expected": all(
            interval.flight[mode]["bots"] == expected_bots
            and interval.gear[mode]["bots"] == expected_bots
            for interval in intervals
            for mode in MODES
        ),
        "control_active_intent_violations": control_active_intents,
        "gear_sample_count_distribution": {
            mode: {str(samples): count for samples, count in sorted(sample_counts[mode].items())}
            for mode in MODES
        },
        "activity_seconds_per_interval": {
            mode: {
                "minimum": min(values) if values else None,
                "median": statistics.median(values) if values else None,
                "maximum": max(values) if values else None,
            }
            for mode, values in activity_seconds.items()
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--expected-bots", type=int, default=40)
    parser.add_argument(
        "--expected-samples",
        type=int,
        help="required gear samples per arm in a complete interval (480 for 40 bots at 5s/60s)",
    )
    parser.add_argument("--exclude-first", type=int, default=1)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--window", type=int, default=30)
    parser.add_argument(
        "--prior-aggregate",
        type=Path,
        help="machine-readable aggregate from an earlier recorder epoch to add to full-run totals",
    )
    args = parser.parse_args()

    parsed = parse_log(args.log, args.run_id)
    retained, exclusions = select_intervals(
        parsed,
        args.expected_bots,
        args.expected_samples,
        args.exclude_first,
        args.limit,
    )
    window = min(args.window, len(retained))
    output = {
        "run_id": args.run_id,
        "parsed_intervals": len(parsed),
        "retained_intervals": len(retained),
        "retained_line_span": [retained[0].first_line, retained[-1].last_line] if retained else None,
        "exclusions": exclusions,
        "full": aggregate(retained),
        "first_window": aggregate(retained[:window]),
        "last_window": aggregate(retained[-window:]),
        "level_bands": aggregate_level_bands(retained),
        "intent_metrics": aggregate_intents(retained),
        "scheduler_metrics": aggregate_scheduler(retained),
        "exposure_integrity": summarize_exposure_integrity(retained, args.expected_bots),
    }
    if args.prior_aggregate:
        output["combined_full"] = combine_with_prior(args.prior_aggregate, args.run_id, output["full"])
    print(json.dumps(output, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
