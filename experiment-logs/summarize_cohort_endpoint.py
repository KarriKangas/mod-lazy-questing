#!/usr/bin/env python3
"""Create a concise, report-ready Cohort 10 endpoint and gate summary.

Inputs are the JSON artifacts emitted by capture_cohort_endpoint.ps1 plus the
machine-readable checkpoint from the lost first recorder epoch. The output is
derived data only and contains no credentials or raw runtime records.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


RATE_FIELDS = (
    "xp",
    "kill_xp",
    "quest_xp",
    "kills",
    "deaths",
    "loot_events",
    "quest_pickups",
    "objective_deltas",
    "quest_completions",
    "quest_turn_ins",
    "level_ups",
    "loot_gear",
    "quest_gear",
    "equip_events",
)
ACTIVITY_FIELDS = ("travel_s", "fight_s", "loot_s", "interact_s", "service_s", "dead_s", "idle_s")
DETAILED_GEAR_FIELDS = (
    "populated_slot_average_item_level",
    "total_item_level",
    "occupied_slots",
    "weapon_item_level",
    "weapon_quality",
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def compact_comparison(comparison: dict[str, Any]) -> dict[str, Any]:
    return {
        "control_n": comparison["control"]["n"],
        "control_mean": comparison["control"]["mean"],
        "control_median": comparison["control"]["median"],
        "assist_n": comparison["assist-only"]["n"],
        "assist_mean": comparison["assist-only"]["mean"],
        "assist_median": comparison["assist-only"]["median"],
        "assist_relative_percent": comparison["relative_percent"],
        "welch_95_mean_difference": comparison["welch_95"],
    }


def compact_recorder(aggregate: dict[str, Any]) -> dict[str, Any]:
    control = aggregate["arms"]["control"]
    assist = aggregate["arms"]["assist-only"]
    comparisons = aggregate["comparisons"]
    return {
        "intervals": aggregate.get(
            "intervals",
            {"control": control["intervals"], "assist-only": assist["intervals"]},
        ),
        "bot_hours": {
            "control": control["bot_hours"],
            "assist-only": assist["bot_hours"],
        },
        "rates": {
            field: {
                "control": control["rates"][field],
                "assist-only": assist["rates"][field],
                "assist_relative_percent": comparisons[f"{field}_rate_relative_percent"],
            }
            for field in RATE_FIELDS
        },
        "loot_per_kill": {
            "control": control["loot_per_kill"],
            "assist-only": assist["loot_per_kill"],
            "assist_relative_percent": comparisons["loot_per_kill_relative_percent"],
        },
        "activity": {
            field: {
                "control_percent": control["activity_percent"][field],
                "assist_percent": assist["activity_percent"][field],
                "percentage_point_difference": comparisons[f"{field}_percentage_point_difference"],
            }
            for field in ACTIVITY_FIELDS
        },
        "gear_event_quality": {
            field: {
                "control": control["item_quality"][field],
                "assist-only": assist["item_quality"][field],
                "assist_relative_percent": comparisons[f"{field}_relative_percent"],
            }
            for field in (
                "loot_gear_average_item_level",
                "quest_gear_average_item_level",
                "equip_event_average_item_level",
            )
        },
        "last_snapshot": {
            "control": control["last_snapshot"],
            "assist-only": assist["last_snapshot"],
        },
    }


def latest_common_milestone(durable: dict[str, Any]) -> tuple[int, dict[str, Any]]:
    eligible: list[int] = []
    for level_text, result in durable["milestones"].items():
        counts = result["arm_counts"]
        if counts.get("control", 0) >= 20 and counts.get("assist-only", 0) >= 20:
            eligible.append(int(level_text))
    if not eligible:
        raise ValueError("no milestone level has at least 20 bots in both arms")
    level = max(eligible)
    return level, durable["milestones"][str(level)]


def same_level_gear(durable: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for level, row in durable["saved_current"]["same_level"].items():
        counts = row["arm_counts"]
        if not counts.get("control") or not counts.get("assist-only"):
            continue
        result[level] = {
            "arm_counts": counts,
            "equipment": {
                field: compact_comparison(row["equipment"][field])
                for field in DETAILED_GEAR_FIELDS
            },
        }
    return result


def latest_level_band(recorder: dict[str, Any]) -> tuple[int | None, dict[str, Any] | None]:
    levels = [int(level) for level in recorder["level_bands"]]
    if not levels:
        return None, None
    level = max(levels)
    return level, recorder["level_bands"][str(level)]


def ge_zero(value: float | None) -> bool | None:
    return None if value is None else value >= 0.0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recorder", type=Path, required=True)
    parser.add_argument("--durable", type=Path, required=True)
    parser.add_argument("--prior-aggregate", type=Path, required=True)
    args = parser.parse_args()

    recorder = load_json(args.recorder)
    durable = load_json(args.durable)
    prior = load_json(args.prior_aggregate)
    full = recorder.get("combined_full")
    if not full:
        raise ValueError("recorder analysis lacks combined_full; run with --prior-aggregate")
    if recorder["run_id"] != prior["run_id"]:
        raise ValueError("recorder and prior run IDs do not match")

    latest_level, milestone = latest_common_milestone(durable)
    band_level, band = latest_level_band(recorder)
    combined = compact_recorder(full)
    last_window = compact_recorder(recorder["last_window"])
    first_window = compact_recorder(recorder["first_window"])
    same_level = same_level_gear(durable)
    full_comparisons = full["comparisons"]
    last_comparisons = recorder["last_window"]["comparisons"]
    band_xp_difference = band["comparisons"]["xp_rate_relative_percent"] if band else None

    milestone_gear = compact_comparison(milestone["item_level"])
    well_sampled_same_level = {
        level: row
        for level, row in same_level.items()
        if row["arm_counts"].get("control", 0) >= 5 and row["arm_counts"].get("assist-only", 0) >= 5
    }
    detailed_gear_checks = {
        f"level_{level}_{field}": ge_zero(metric["assist_relative_percent"])
        for level, row in well_sampled_same_level.items()
        for field, metric in row["equipment"].items()
    }

    gates = {
        "activation": {
            "pass": prior["intent_audit"]["assessment"] == "passed",
            **prior["intent_audit"],
            "second_epoch_acquired": recorder["intent_metrics"]["acquired"],
        },
        "xp_no_more_than_2_percent_below_control": {
            "value_percent": full_comparisons["xp_rate_relative_percent"],
            "pass": full_comparisons["xp_rate_relative_percent"] >= -2.0,
        },
        "late_behavior_no_more_than_5_percent_below_control": {
            "last_window_xp_percent": last_comparisons["xp_rate_relative_percent"],
            "latest_recorder_band": band_level,
            "latest_band_xp_percent": band_xp_difference,
            "pass": (
                last_comparisons["xp_rate_relative_percent"] >= -5.0
                and (band_xp_difference is None or band_xp_difference >= -5.0)
            ),
        },
        "loot_throughput_preserved": {
            "loot_event_rate_percent": full_comparisons["loot_events_rate_relative_percent"],
            "loot_per_kill_percent": full_comparisons["loot_per_kill_relative_percent"],
            "equippable_loot_rate_percent": full_comparisons["loot_gear_rate_relative_percent"],
            "pass": (
                full_comparisons["loot_events_rate_relative_percent"] >= 0.0
                and full_comparisons["loot_per_kill_relative_percent"] >= 0.0
                and full_comparisons["loot_gear_rate_relative_percent"] >= 0.0
            ),
        },
        "gear_preserved": {
            "latest_common_milestone_level": latest_level,
            "milestone_item_level_percent": milestone_gear["assist_relative_percent"],
            "quest_gear_rate_percent": full_comparisons["quest_gear_rate_relative_percent"],
            "equip_event_rate_percent": full_comparisons["equip_events_rate_relative_percent"],
            "well_sampled_same_level_checks": detailed_gear_checks,
            "pass": (
                ge_zero(milestone_gear["assist_relative_percent"]) is True
                and full_comparisons["quest_gear_rate_relative_percent"] >= 0.0
                and full_comparisons["equip_events_rate_relative_percent"] >= 0.0
                and bool(detailed_gear_checks)
                and all(value is True for value in detailed_gear_checks.values())
            ),
        },
        "travel_increase_under_3pp": {
            "value_pp": full_comparisons["travel_s_percentage_point_difference"],
            "pass": full_comparisons["travel_s_percentage_point_difference"] < 3.0,
        },
        "effectively_zero_hard_stalls": {
            "prior_hard_stalls": prior["intent_audit"]["hard_stalls"],
            "second_epoch_hard_stalls": recorder["scheduler_metrics"]["hard_stalls"],
            "pass": prior["intent_audit"]["hard_stalls"] + recorder["scheduler_metrics"]["hard_stalls"] == 0,
        },
        "median_time_to_level_at_least_5_percent_faster": {
            "latest_common_milestone_level": latest_level,
            "value_percent": milestone["fixed_exposure_time_to_level"]["median_time_improvement_percent"],
            "without_zurmos_percent": milestone["fixed_exposure_time_to_level_without_zurmos"][
                "median_time_improvement_percent"
            ],
            "pass": milestone["fixed_exposure_time_to_level"]["median_time_improvement_percent"] >= 5.0,
        },
    }

    output = {
        "run_id": recorder["run_id"],
        "assignment": durable["assignment"],
        "recorder_exclusions": recorder["exclusions"],
        "second_epoch_exposure_integrity": recorder["exposure_integrity"],
        "combined_recorder": combined,
        "second_epoch_first_window": first_window,
        "second_epoch_last_window": last_window,
        "second_epoch_level_bands": {
            level: compact_recorder(result) for level, result in recorder["level_bands"].items()
        },
        "durable_saved_metrics": {
            field: compact_comparison(durable["saved_current"]["metrics"][field])
            for field in (
                "level",
                "total_progression_xp",
                "xp_per_played_hour",
                "rewarded_quests",
                "money",
                *DETAILED_GEAR_FIELDS,
            )
        },
        "same_saved_level_gear": same_level,
        "latest_common_milestone": {
            "level": latest_level,
            "arm_counts": milestone["arm_counts"],
            "fixed_exposure_time_to_level": milestone["fixed_exposure_time_to_level"],
            "fixed_exposure_time_to_level_without_zurmos": milestone[
                "fixed_exposure_time_to_level_without_zurmos"
            ],
            "played_seconds": compact_comparison(milestone["played_seconds"]),
            "quests_completed": compact_comparison(milestone["quests_completed"]),
            "item_level": milestone_gear,
        },
        "tails": durable["tails"],
        "zurmos": durable["zurmos"],
        "intent_audit": {
            "prior": prior["intent_audit"],
            "second_epoch": recorder["intent_metrics"],
            "second_epoch_scheduler": recorder["scheduler_metrics"],
        },
        "gates": gates,
        "all_gates_pass": all(gate["pass"] for gate in gates.values()),
    }
    print(json.dumps(output, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
