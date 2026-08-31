#!/usr/bin/env python3
"""Analyze cohort assignment, durable level milestones, and saved equipment endpoints.

The script reproduces the module's unsigned 64-bit experiment hash. It reads the
character/world database connection strings from worldserver.conf, passes the password
to mysql only through its process environment, and writes no credentials or raw records.
Online saved character/equipment rows can lag; use Aquarium for the final live snapshot.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import statistics
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


MASK64 = (1 << 64) - 1
RACES = {
    1: "Human",
    2: "Orc",
    3: "Dwarf",
    4: "Night Elf",
    5: "Undead",
    6: "Tauren",
    7: "Gnome",
    8: "Troll",
    10: "Blood Elf",
    11: "Draenei",
}
CLASSES = {
    1: "Warrior",
    2: "Paladin",
    3: "Hunter",
    4: "Rogue",
    5: "Priest",
    7: "Shaman",
    8: "Mage",
    9: "Warlock",
    11: "Druid",
}


def read_database_info(config_path: Path, key: str) -> tuple[str, str, str, str, str]:
    pattern = re.compile(rf"^\s*{re.escape(key)}\s*=\s*\"?([^\"]+)\"?\s*$")
    value: str | None = None
    for line in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            value = match.group(1).strip()
    if value is None:
        raise RuntimeError(f"{key} not found in {config_path}")
    parts = value.split(";")
    if len(parts) < 5:
        raise RuntimeError(f"unexpected {key} format")
    host, port, user, password, database = parts[:5]
    if not re.fullmatch(r"[A-Za-z0-9_]+", database):
        raise RuntimeError(f"unsafe database identifier {database!r}")
    return host, port, user, password, database


def mysql_rows(connection: tuple[str, str, str, str, str], sql: str) -> list[dict[str, str]]:
    host, port, user, password, database = connection
    environment = os.environ.copy()
    environment["MYSQL_PWD"] = password
    command = [
        "mysql",
        f"--host={host}",
        f"--port={port}",
        f"--user={user}",
        f"--database={database}",
        "--batch",
        "--raw",
        f"--execute={sql}",
    ]
    completed = subprocess.run(command, check=True, capture_output=True, text=True, env=environment)
    return list(csv.DictReader(completed.stdout.splitlines(), delimiter="\t"))


def experiment_bucket(guid: int, race: int, class_id: int, seed: int) -> int:
    value = guid & MASK64
    value ^= (race << 48) & MASK64
    value ^= (class_id << 56) & MASK64
    value ^= (seed * 0x9E3779B97F4A7C15) & MASK64
    value ^= value >> 33
    value = (value * 0xFF51AFD7ED558CCD) & MASK64
    value ^= value >> 33
    value = (value * 0xC4CEB9FE1A85EC53) & MASK64
    value ^= value >> 33
    return value % 100


def mode_for(bucket: int, control_percent: int, assist_percent: int) -> str:
    if bucket < control_percent:
        return "control"
    if bucket < control_percent + assist_percent:
        return "assist-only"
    return "current"


def number(value: str | None, integer: bool = False) -> float | int:
    if value in (None, "", "NULL"):
        return 0 if integer else 0.0
    return int(value) if integer else float(value)


def metric(values: Iterable[float]) -> dict[str, float | int | None]:
    data = list(values)
    if not data:
        return {"n": 0, "mean": None, "median": None, "sd": None}
    return {
        "n": len(data),
        "mean": statistics.fmean(data),
        "median": statistics.median(data),
        "sd": statistics.stdev(data) if len(data) > 1 else 0.0,
    }


def compare(control_values: Iterable[float], treatment_values: Iterable[float]) -> dict[str, Any]:
    control = metric(control_values)
    treatment = metric(treatment_values)
    result: dict[str, Any] = {"control": control, "assist-only": treatment}
    if not control["n"] or not treatment["n"]:
        result.update({"mean_difference": None, "median_difference": None, "relative_percent": None, "welch_95": None})
        return result
    mean_difference = float(treatment["mean"]) - float(control["mean"])
    median_difference = float(treatment["median"]) - float(control["median"])
    relative_percent = None if control["mean"] == 0 else mean_difference / float(control["mean"]) * 100.0
    se = math.sqrt(
        float(control["sd"]) ** 2 / int(control["n"])
        + float(treatment["sd"]) ** 2 / int(treatment["n"])
    )
    result.update(
        {
            "mean_difference": mean_difference,
            "median_difference": median_difference,
            "relative_percent": relative_percent,
            "welch_95": [mean_difference - 1.96 * se, mean_difference + 1.96 * se],
        }
    )
    return result


def arm_compare(rows: list[dict[str, Any]], field: str) -> dict[str, Any]:
    return compare(
        (float(row[field]) for row in rows if row["mode"] == "control"),
        (float(row[field]) for row in rows if row["mode"] == "assist-only"),
    )


def kaplan_meier_median(observations: list[tuple[float, bool]]) -> float | None:
    """Return the earliest time at which estimated survival is at most 0.5."""
    if not observations:
        return None
    survival = 1.0
    for time in sorted({value for value, _ in observations}):
        at_risk = sum(1 for value, _ in observations if value >= time)
        events = sum(1 for value, event in observations if value == time and event)
        if not at_risk or not events:
            continue
        survival *= 1.0 - events / at_risk
        if survival <= 0.5:
            return time
    return None


def time_to_level_summary(
    characters: list[dict[str, Any]],
    milestones: list[dict[str, Any]],
    level: int,
    exposure_seconds: float | None = None,
) -> dict[str, Any]:
    event_by_guid = {
        row["guid"]: float(row["played_seconds"])
        for row in milestones
        if row["level"] == level
    }
    result: dict[str, Any] = {}
    latest_milestone_by_guid: defaultdict[int, float] = defaultdict(float)
    for row in milestones:
        latest_milestone_by_guid[row["guid"]] = max(
            latest_milestone_by_guid[row["guid"]], float(row["played_seconds"])
        )
    for mode in ("control", "assist-only"):
        arm = [row for row in characters if row["mode"] == mode]
        observations = [
            (
                event_by_guid.get(
                    row["guid"],
                    exposure_seconds
                    if exposure_seconds is not None
                    else max(float(row["totaltime"]), latest_milestone_by_guid[row["guid"]]),
                ),
                row["guid"] in event_by_guid,
            )
            for row in arm
        ]
        reached = sum(1 for _, event in observations if event)
        result[mode] = {
            "n": len(observations),
            "reached": reached,
            "reach_percent": reached / len(observations) * 100.0 if observations else None,
            "kaplan_meier_median_seconds": kaplan_meier_median(observations),
        }
    control_median = result["control"]["kaplan_meier_median_seconds"]
    treatment_median = result["assist-only"]["kaplan_meier_median_seconds"]
    result["median_time_improvement_percent"] = (
        None
        if control_median in (None, 0) or treatment_median is None
        else (float(control_median) - float(treatment_median)) / float(control_median) * 100.0
    )
    return result


def group_balance(characters: list[dict[str, Any]], field: str, labels: dict[int, str] | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {}
    values = sorted({int(row[field]) if field in ("race", "class") else row[field] for row in characters})
    for value in values:
        subset = [row for row in characters if row[field] == value]
        name = labels.get(int(value), str(value)) if labels else str(value)
        counts = Counter(row["mode"] for row in subset)
        result[name] = {"total": len(subset), **{mode: counts.get(mode, 0) for mode in ("control", "assist-only", "current")}}
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worldserver-conf", type=Path, required=True)
    parser.add_argument("--guild-ids", required=True, help="comma-separated integer guild IDs")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--control-percent", type=int, default=50)
    parser.add_argument("--assist-percent", type=int, default=50)
    parser.add_argument("--tail-level", type=int)
    parser.add_argument(
        "--exposure-seconds",
        type=float,
        help="common right-censor time since first login; use 25200 at the preregistered seven-hour endpoint",
    )
    parser.add_argument(
        "--summary-only",
        action="store_true",
        help="emit a concise monitoring/endpoint summary instead of the full diagnostic report",
    )
    args = parser.parse_args()

    guild_ids = [int(value.strip()) for value in args.guild_ids.split(",")]
    if not guild_ids:
        raise RuntimeError("at least one guild ID is required")
    guild_clause = ",".join(str(value) for value in guild_ids)
    character_connection = read_database_info(args.worldserver_conf, "CharacterDatabaseInfo")
    world_connection = read_database_info(args.worldserver_conf, "WorldDatabaseInfo")
    world_database = world_connection[4]
    if character_connection[:3] != world_connection[:3]:
        raise RuntimeError("character and world databases must be reachable through the same mysql connection")

    character_sql = f"""
SELECT c.guid,c.name,c.race,c.class,c.level,c.xp,c.money,c.totaltime,c.online,c.position_x,c.position_y,c.position_z,c.map,c.zone,
       COUNT(q.quest) AS rewarded_quests,g.name AS guild_name,g.guildid
FROM guild_member gm
JOIN guild g ON g.guildid=gm.guildid
JOIN characters c ON c.guid=gm.guid
LEFT JOIN character_queststatus_rewarded q ON q.guid=c.guid
WHERE gm.guildid IN ({guild_clause})
GROUP BY c.guid,c.name,c.race,c.class,c.level,c.xp,c.money,c.totaltime,c.online,c.position_x,c.position_y,c.position_z,c.map,c.zone,g.name,g.guildid
ORDER BY c.guid;
"""
    milestone_sql = f"""
SELECT l.character_guid,l.level,l.level_up_at,l.played_since_first_login_seconds,l.quests_completed_amount,l.item_level
FROM strict_altbot_levelups l
JOIN guild_member gm ON gm.guid=l.character_guid
WHERE gm.guildid IN ({guild_clause})
ORDER BY l.character_guid,l.level;
"""
    equipment_sql = f"""
SELECT c.guid,
       COUNT(ci.item) AS occupied_slots,
       COALESCE(SUM(it.ItemLevel),0) AS total_item_level,
       COALESCE(ROUND(AVG(it.ItemLevel),4),0) AS populated_slot_average_item_level,
       COALESCE(MAX(CASE WHEN ci.slot IN (15,17) THEN it.ItemLevel ELSE 0 END),0) AS weapon_item_level,
       COALESCE(MAX(CASE WHEN ci.slot IN (15,17) THEN it.Quality ELSE 0 END),0) AS weapon_quality
FROM guild_member gm
JOIN characters c ON c.guid=gm.guid
LEFT JOIN character_inventory ci ON ci.guid=c.guid AND ci.bag=0 AND ci.slot BETWEEN 0 AND 18
LEFT JOIN item_instance ii ON ii.guid=ci.item
LEFT JOIN `{world_database}`.item_template it ON it.entry=ii.itemEntry
WHERE gm.guildid IN ({guild_clause})
GROUP BY c.guid
ORDER BY c.guid;
"""

    character_rows = mysql_rows(character_connection, character_sql)
    milestone_rows = mysql_rows(character_connection, milestone_sql)
    equipment_rows = mysql_rows(character_connection, equipment_sql)
    xp_rows = mysql_rows(
        character_connection,
        f"SELECT Level,Experience FROM `{world_database}`.player_xp_for_level ORDER BY Level;",
    )
    xp_by_level = {int(row["Level"]): int(row["Experience"]) for row in xp_rows}
    equipment_by_guid = {int(row["guid"]): row for row in equipment_rows}

    characters: list[dict[str, Any]] = []
    for row in character_rows:
        guid = int(row["guid"])
        race = int(row["race"])
        class_id = int(row["class"])
        level = int(row["level"])
        total_progression_xp = sum(xp_by_level.get(source_level, 0) for source_level in range(1, level)) + int(row["xp"])
        played_seconds = int(row["totaltime"])
        bucket = experiment_bucket(guid, race, class_id, args.seed)
        equipment = equipment_by_guid.get(guid, {})
        characters.append(
            {
                "guid": guid,
                "name": row["name"],
                "race": race,
                "race_name": RACES.get(race, str(race)),
                "class": class_id,
                "class_name": CLASSES.get(class_id, str(class_id)),
                "guild_id": int(row["guildid"]),
                "guild_name": row["guild_name"],
                "bucket": bucket,
                "mode": mode_for(bucket, args.control_percent, args.assist_percent),
                "level": level,
                "xp": int(row["xp"]),
                "total_progression_xp": total_progression_xp,
                "xp_per_played_hour": total_progression_xp / (played_seconds / 3600.0) if played_seconds else 0.0,
                "money": int(row["money"]),
                "totaltime": played_seconds,
                "online": int(row["online"]),
                "rewarded_quests": int(row["rewarded_quests"]),
                "position_x": float(row["position_x"]),
                "position_y": float(row["position_y"]),
                "position_z": float(row["position_z"]),
                "map": int(row["map"]),
                "zone": int(row["zone"]),
                "occupied_slots": int(number(equipment.get("occupied_slots"), True)),
                "total_item_level": int(number(equipment.get("total_item_level"), True)),
                "populated_slot_average_item_level": float(number(equipment.get("populated_slot_average_item_level"))),
                "weapon_item_level": int(number(equipment.get("weapon_item_level"), True)),
                "weapon_quality": int(number(equipment.get("weapon_quality"), True)),
            }
        )
    characters_by_guid = {row["guid"]: row for row in characters}

    milestones: list[dict[str, Any]] = []
    for row in milestone_rows:
        guid = int(row["character_guid"])
        character = characters_by_guid.get(guid)
        if character is None:
            continue
        milestones.append(
            {
                "guid": guid,
                "name": character["name"],
                "race_name": character["race_name"],
                "class_name": character["class_name"],
                "mode": character["mode"],
                "level": int(row["level"]),
                "level_up_at": row["level_up_at"],
                "played_seconds": int(row["played_since_first_login_seconds"]),
                "quests_completed": int(row["quests_completed_amount"]),
                "item_level": int(row["item_level"]),
            }
        )
    endpoint_milestones = (
        [row for row in milestones if row["played_seconds"] <= args.exposure_seconds]
        if args.exposure_seconds is not None
        else milestones
    )

    assignment_counts = Counter(row["mode"] for row in characters)
    current_metrics = {
        field: arm_compare(characters, field)
        for field in (
            "level",
            "total_progression_xp",
            "xp_per_played_hour",
            "money",
            "totaltime",
            "rewarded_quests",
            "populated_slot_average_item_level",
            "total_item_level",
            "occupied_slots",
            "weapon_item_level",
            "weapon_quality",
        )
    }
    sensitivity_characters = [row for row in characters if row["guid"] != 2537]
    sensitivity_metrics = {
        field: arm_compare(sensitivity_characters, field)
        for field in (
            "level",
            "total_progression_xp",
            "xp_per_played_hour",
            "money",
            "rewarded_quests",
            "populated_slot_average_item_level",
            "total_item_level",
            "occupied_slots",
            "weapon_item_level",
            "weapon_quality",
        )
    }
    current_by_level: dict[str, Any] = {}
    for level in sorted({row["level"] for row in characters}):
        subset = [row for row in characters if row["level"] == level]
        current_by_level[str(level)] = {
            "arm_counts": dict(Counter(row["mode"] for row in subset)),
            "equipment": {
                field: arm_compare(subset, field)
                for field in (
                    "populated_slot_average_item_level",
                    "total_item_level",
                    "occupied_slots",
                    "weapon_item_level",
                    "weapon_quality",
                )
            },
        }

    milestone_by_level: dict[str, Any] = {}
    levels = sorted({row["level"] for row in endpoint_milestones})
    for level in levels:
        subset = [row for row in endpoint_milestones if row["level"] == level]
        time_comparison = arm_compare(subset, "played_seconds")
        control_median = time_comparison["control"]["median"]
        treatment_median = time_comparison["assist-only"]["median"]
        median_time_improvement = (
            None
            if control_median in (None, 0) or treatment_median is None
            else (float(control_median) - float(treatment_median)) / float(control_median) * 100.0
        )
        milestone_by_level[str(level)] = {
            "arm_counts": dict(Counter(row["mode"] for row in subset)),
            "played_seconds": time_comparison,
            "median_time_improvement_percent": median_time_improvement,
            "quests_completed": arm_compare(subset, "quests_completed"),
            "item_level": arm_compare(subset, "item_level"),
            "fixed_exposure_time_to_level": time_to_level_summary(
                characters, endpoint_milestones, level, args.exposure_seconds
            ),
            "fixed_exposure_time_to_level_without_zurmos": time_to_level_summary(
                sensitivity_characters,
                [row for row in endpoint_milestones if row["guid"] != 2537],
                level,
                args.exposure_seconds,
            ),
        }

    if args.tail_level is not None:
        tail_level = args.tail_level
    else:
        tail_level = max(
            (
                level
                for level in levels
                if sum(1 for row in endpoint_milestones if row["level"] == level and row["mode"] == "control") >= 20
                and sum(1 for row in endpoint_milestones if row["level"] == level and row["mode"] == "assist-only") >= 20
            ),
            default=max(levels, default=0),
        )
    tail_rows = [row for row in endpoint_milestones if row["level"] == tail_level]
    tails: dict[str, Any] = {}
    for category in ("class_name", "race_name"):
        groups: dict[str, Any] = {}
        for name in sorted({row[category] for row in tail_rows}):
            subset = [row for row in tail_rows if row[category] == name]
            groups[name] = {
                "arm_counts": dict(Counter(row["mode"] for row in subset)),
                "played_seconds": arm_compare(subset, "played_seconds"),
                "item_level": arm_compare(subset, "item_level"),
            }
        tails[category] = groups

    worst = sorted(tail_rows, key=lambda row: row["played_seconds"], reverse=True)[:12]
    zurmos = next((row for row in characters if row["guid"] == 2537), None)
    zurmos_milestones = [row for row in endpoint_milestones if row["guid"] == 2537]
    output = {
        "assignment": {
            "seed": args.seed,
            "guild_ids": guild_ids,
            "counts": dict(assignment_counts),
            "factions": group_balance(characters, "guild_name"),
            "races": group_balance(characters, "race", RACES),
            "classes": group_balance(characters, "class", CLASSES),
        },
        "online_count": sum(row["online"] for row in characters),
        "saved_current": {
            "metrics": current_metrics,
            "same_level": current_by_level,
            "sensitivity_without_zurmos": sensitivity_metrics,
        },
        "milestones": milestone_by_level,
        "post_endpoint_milestones_excluded": len(milestones) - len(endpoint_milestones),
        "tails": {"level": tail_level, **tails, "worst_played": worst},
        "zurmos": {"saved_current": zurmos, "milestones": zurmos_milestones},
        "caveat": "Saved current character/equipment rows may lag while bots are online; use Aquarium for the final live snapshot.",
    }

    if args.summary_only:
        latest_milestone_seconds: defaultdict[int, int] = defaultdict(int)
        latest_milestone_level: defaultdict[int, int] = defaultdict(int)
        for row in milestones:
            latest_milestone_seconds[row["guid"]] = max(
                latest_milestone_seconds[row["guid"]], int(row["played_seconds"])
            )
            latest_milestone_level[row["guid"]] = max(
                latest_milestone_level[row["guid"]], int(row["level"])
            )

        exposure_rows = [
            {
                "guid": row["guid"],
                "mode": row["mode"],
                "seconds": max(int(row["totaltime"]), latest_milestone_seconds[row["guid"]]),
            }
            for row in characters
        ]

        def exposure_stats(rows: list[dict[str, Any]]) -> dict[str, Any]:
            seconds = [int(row["seconds"]) for row in rows]
            target = args.exposure_seconds
            return {
                "n": len(seconds),
                "minimum_seconds": min(seconds) if seconds else None,
                "median_seconds": statistics.median(seconds) if seconds else None,
                "maximum_seconds": max(seconds) if seconds else None,
                "at_or_above_target": (
                    sum(value >= target for value in seconds) if target is not None else None
                ),
                "maximum_remaining_seconds": (
                    max(0.0, target - min(seconds)) if seconds and target is not None else None
                ),
            }

        attained_level_distribution: dict[str, dict[str, int]] = {}
        for mode in ("control", "assist-only"):
            counts = Counter(
                max(int(row["level"]), latest_milestone_level[row["guid"]])
                for row in characters
                if row["mode"] == mode
            )
            attained_level_distribution[mode] = {
                str(level): count for level, count in sorted(counts.items())
            }

        without_zurmos = [row for row in exposure_rows if row["guid"] != 2537]
        target = args.exposure_seconds

        def compact_comparison(comparison: dict[str, Any]) -> dict[str, Any]:
            return {
                "control_mean": comparison["control"]["mean"],
                "control_median": comparison["control"]["median"],
                "assist_mean": comparison["assist-only"]["mean"],
                "assist_median": comparison["assist-only"]["median"],
                "mean_relative_percent": comparison["relative_percent"],
                "welch_95_mean_difference": comparison["welch_95"],
            }

        latest_common = milestone_by_level.get(str(tail_level))
        summary = {
            "assignment_counts": dict(assignment_counts),
            "online_count_saved": sum(row["online"] for row in characters),
            "exposure": {
                "target_seconds": target,
                "all_bots_observed_floor": exposure_stats(exposure_rows),
                "without_zurmos_observed_floor": exposure_stats(without_zurmos),
                "by_arm_observed_floor": {
                    mode: exposure_stats([row for row in exposure_rows if row["mode"] == mode])
                    for mode in ("control", "assist-only")
                },
                "endpoint_ready_without_zurmos": (
                    all(row["seconds"] >= target for row in without_zurmos)
                    if target is not None
                    else None
                ),
                "authority_caveat": (
                    "This is a conservative durable floor from saved characters and attained-level milestones; "
                    "online in-memory played time may be higher. Use Aquarium at the endpoint."
                ),
            },
            "attained_level_distribution": attained_level_distribution,
            "saved_headline_metrics": {
                field: compact_comparison(current_metrics[field])
                for field in (
                    "total_progression_xp",
                    "xp_per_played_hour",
                    "rewarded_quests",
                    "populated_slot_average_item_level",
                    "total_item_level",
                    "occupied_slots",
                    "weapon_item_level",
                    "weapon_quality",
                )
            },
            "latest_common_milestone": {
                "level": tail_level,
                "arm_counts": latest_common["arm_counts"] if latest_common else None,
                "fixed_exposure_time_to_level": (
                    latest_common["fixed_exposure_time_to_level"] if latest_common else None
                ),
                "fixed_exposure_time_to_level_without_zurmos": (
                    latest_common["fixed_exposure_time_to_level_without_zurmos"]
                    if latest_common
                    else None
                ),
                "milestone_item_level": (
                    compact_comparison(latest_common["item_level"]) if latest_common else None
                ),
                "milestone_quests_completed": (
                    compact_comparison(latest_common["quests_completed"]) if latest_common else None
                ),
            },
            "zurmos": {
                "saved_current": zurmos,
                "latest_milestone": zurmos_milestones[-1] if zurmos_milestones else None,
            },
        }
        print(json.dumps(summary, indent=2, sort_keys=True))
        return
    print(json.dumps(output, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
