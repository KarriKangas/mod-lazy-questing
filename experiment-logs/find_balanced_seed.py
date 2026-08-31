#!/usr/bin/env python3
"""Find a reproducibly balanced Lazy Questing experiment seed for two cohort guilds.

The roster is read from the character database named by worldserver.conf. Database
credentials stay in the mysql subprocess environment and are never printed. A seed is
accepted only when the 50/50 arms are equal overall and within each guild, every
even-sized race/class is exactly split, and every odd-sized race/class differs by one.
When more than one perfect candidate is requested, the selected seed minimizes the
largest pre-experiment arm imbalance across the frozen roster's equipment metrics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from analyze_durable_endpoints import (
    CLASSES,
    RACES,
    experiment_bucket,
    mysql_rows,
    read_database_info,
)


UINT32_MAX = (1 << 32) - 1
HASH_SEED_MULTIPLIER = np.uint64(0x9E3779B97F4A7C15)
HASH_MIX_1 = np.uint64(0xFF51AFD7ED558CCD)
HASH_MIX_2 = np.uint64(0xC4CEB9FE1A85EC53)
EQUIPMENT_FIELDS = (
    "populated_slot_average_item_level",
    "total_item_level",
    "occupied_slots",
    "weapon_item_level",
    "weapon_quality",
)


def parse_ids(text: str) -> list[int]:
    ids = [int(value.strip()) for value in text.split(",") if value.strip()]
    if len(ids) != 2 or len(set(ids)) != 2 or any(value <= 0 for value in ids):
        raise ValueError("--guild-ids must contain exactly two distinct positive IDs")
    return ids


def assign(rows: list[dict[str, Any]], seed: int) -> list[dict[str, Any]]:
    assigned: list[dict[str, Any]] = []
    for row in rows:
        bucket = experiment_bucket(row["guid"], row["race"], row["class"], seed)
        assigned.append({**row, "bucket": bucket, "mode": "control" if bucket < 50 else "assist-only"})
    return assigned


def category_balance(rows: list[dict[str, Any]], field: str, names: dict[int, str] | None = None) -> dict[str, Any]:
    grouped: dict[Any, Counter[str]] = defaultdict(Counter)
    for row in rows:
        grouped[row[field]][row["mode"]] += 1

    result: dict[str, Any] = {}
    for value in sorted(grouped, key=str):
        counts = grouped[value]
        total = counts["control"] + counts["assist-only"]
        label = names.get(value, str(value)) if names else str(value)
        result[label] = {
            "total": total,
            "control": counts["control"],
            "assist-only": counts["assist-only"],
            "optimal": abs(counts["control"] - counts["assist-only"]) == total % 2,
        }
    return result


def summarize(rows: list[dict[str, Any]], seed: int, guild_names: dict[int, str]) -> dict[str, Any]:
    assigned = assign(rows, seed)
    modes = Counter(row["mode"] for row in assigned)
    guilds = category_balance(assigned, "guild_id", guild_names)
    races = category_balance(assigned, "race", RACES)
    classes = category_balance(assigned, "class", CLASSES)
    perfect = (
        modes["control"] == modes["assist-only"]
        and all(value["optimal"] for value in guilds.values())
        and all(value["optimal"] for value in races.values())
        and all(value["optimal"] for value in classes.values())
    )
    return {
        "seed": seed,
        "perfect": perfect,
        "arm_counts": {"control": modes["control"], "assist-only": modes["assist-only"]},
        "guilds": guilds,
        "races": races,
        "classes": classes,
        "starting_equipment": equipment_balance(assigned),
    }


def equipment_balance(rows: list[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for field in EQUIPMENT_FIELDS:
        control = [float(row[field]) for row in rows if row["mode"] == "control"]
        treatment = [float(row[field]) for row in rows if row["mode"] == "assist-only"]
        control_mean = float(np.mean(control))
        treatment_mean = float(np.mean(treatment))
        pooled_mean = float(np.mean(control + treatment))
        difference = treatment_mean - control_mean
        result[field] = {
            "control_mean": control_mean,
            "assist_only_mean": treatment_mean,
            "difference": difference,
            "absolute_difference_percent_of_pooled_mean": (
                abs(difference) / abs(pooled_mean) * 100.0 if pooled_mean else 0.0
            ),
        }
    return result


def equipment_score(control: np.ndarray, equipment: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return maximum and RMS relative arm imbalance for each assignment row."""

    arm_size = control.shape[1] // 2
    control_means = control.astype(np.float64) @ equipment / arm_size
    treatment_means = (equipment.sum(axis=0) - control.astype(np.float64) @ equipment) / arm_size
    pooled_means = equipment.mean(axis=0)
    denominators = np.where(np.abs(pooled_means) > 0.0, np.abs(pooled_means), 1.0)
    relative = np.abs(treatment_means - control_means) / denominators * 100.0
    return relative.max(axis=1), np.sqrt(np.mean(relative * relative, axis=1))


def find_perfect_seed(
    rows: list[dict[str, Any]], start_seed: int, max_seed: int,
    excluded: set[int], batch_size: int, perfect_candidates: int,
) -> tuple[int | None, int, int, dict[str, float] | None]:
    """Vectorize the unsigned hash over seed batches while preserving C++ wraparound."""

    base = np.asarray(
        [
            row["guid"] ^ (row["race"] << 48) ^ (row["class"] << 56)
            for row in rows
        ],
        dtype=np.uint64,
    )
    groups: list[np.ndarray] = []
    for field in ("guild_id", "race", "class"):
        for value in sorted({row[field] for row in rows}):
            groups.append(np.asarray([index for index, row in enumerate(rows) if row[field] == value]))

    checked = 0
    perfect_evaluated = 0
    best_seed: int | None = None
    best_score: tuple[float, float, int] | None = None
    equipment = np.asarray(
        [[float(row[field]) for field in EQUIPMENT_FIELDS] for row in rows],
        dtype=np.float64,
    )
    for batch_start in range(start_seed, max_seed + 1, batch_size):
        batch_end = min(max_seed + 1, batch_start + batch_size)
        seeds = np.arange(batch_start, batch_end, dtype=np.uint64)
        values = base[np.newaxis, :] ^ seeds[:, np.newaxis] * HASH_SEED_MULTIPLIER
        values ^= values >> np.uint64(33)
        values *= HASH_MIX_1
        values ^= values >> np.uint64(33)
        values *= HASH_MIX_2
        values ^= values >> np.uint64(33)
        control = values % np.uint64(100) < np.uint64(50)

        valid = control.sum(axis=1) == len(rows) // 2
        for indices in groups:
            total = len(indices)
            control_count = control[:, indices].sum(axis=1)
            valid &= np.abs(control_count * 2 - total) == total % 2

        if excluded:
            valid &= ~np.isin(seeds, np.fromiter(excluded, dtype=np.uint64))

        matches = np.flatnonzero(valid)
        if matches.size:
            remaining = perfect_candidates - perfect_evaluated
            matches = matches[:remaining]
            max_relative, rms_relative = equipment_score(control[matches], equipment)
            for index, maximum, rms in zip(matches, max_relative, rms_relative):
                candidate_seed = int(seeds[int(index)])
                score = (float(maximum), float(rms), candidate_seed)
                if best_score is None or score < best_score:
                    best_score = score
                    best_seed = candidate_seed
            perfect_evaluated += len(matches)
            if perfect_evaluated >= perfect_candidates:
                last_seed = int(seeds[int(matches[-1])])
                checked += last_seed - batch_start + 1 - sum(
                    1 for seed in excluded if batch_start <= seed <= last_seed
                )
                return best_seed, checked, perfect_evaluated, {
                    "maximum_absolute_percent": best_score[0],
                    "rms_percent": best_score[1],
                }

        checked += len(seeds) - sum(1 for seed in excluded if batch_start <= seed < batch_end)

    score_output = None if best_score is None else {
        "maximum_absolute_percent": best_score[0],
        "rms_percent": best_score[1],
    }
    return best_seed, checked, perfect_evaluated, score_output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worldserver-conf", type=Path, required=True)
    parser.add_argument("--guild-ids", required=True, help="two comma-separated cohort guild IDs")
    parser.add_argument("--start-seed", type=int, default=1)
    parser.add_argument("--max-seed", type=int, default=10_000_000)
    parser.add_argument("--exclude-seed", type=int, action="append", default=[])
    parser.add_argument("--batch-size", type=int, default=100_000)
    parser.add_argument(
        "--perfect-candidates",
        type=int,
        default=1,
        help="rank this many categorically perfect seeds by frozen starting-equipment balance",
    )
    args = parser.parse_args()

    guild_ids = parse_ids(args.guild_ids)
    if args.start_seed < 0 or args.max_seed < args.start_seed or args.max_seed > UINT32_MAX:
        raise ValueError("seed range must be ordered and fit an unsigned 32-bit value")
    if args.batch_size < 1 or args.batch_size > 1_000_000:
        raise ValueError("--batch-size must be between 1 and 1000000")
    if args.perfect_candidates < 1:
        raise ValueError("--perfect-candidates must be positive")

    connection = read_database_info(args.worldserver_conf, "CharacterDatabaseInfo")
    world_database = read_database_info(args.worldserver_conf, "WorldDatabaseInfo")[4]
    guild_clause = ",".join(str(value) for value in guild_ids)
    raw_rows = mysql_rows(
        connection,
        f"""
SELECT c.guid,c.name,c.race,c.class,g.guildid,g.name AS guild_name,
       COUNT(ci.item) AS occupied_slots,
       COALESCE(SUM(it.ItemLevel),0) AS total_item_level,
       COALESCE(ROUND(AVG(it.ItemLevel),4),0) AS populated_slot_average_item_level,
       COALESCE(MAX(CASE WHEN ci.slot IN (15,17) THEN it.ItemLevel ELSE 0 END),0) AS weapon_item_level,
       COALESCE(MAX(CASE WHEN ci.slot IN (15,17) THEN it.Quality ELSE 0 END),0) AS weapon_quality
FROM guild_member gm
JOIN guild g ON g.guildid=gm.guildid
JOIN characters c ON c.guid=gm.guid
JOIN strict_altbots sa ON sa.character_guid=c.guid
LEFT JOIN character_inventory ci ON ci.guid=c.guid AND ci.bag=0 AND ci.slot BETWEEN 0 AND 18
LEFT JOIN item_instance ii ON ii.guid=ci.item
LEFT JOIN `{world_database}`.item_template it ON it.entry=ii.itemEntry
WHERE gm.guildid IN ({guild_clause})
  AND sa.enabled=1 AND sa.retired_at IS NULL
GROUP BY c.guid,c.name,c.race,c.class,g.guildid,g.name
ORDER BY c.guid;
""",
    )
    rows = [
        {
            "guid": int(row["guid"]),
            "name": row["name"],
            "race": int(row["race"]),
            "class": int(row["class"]),
            "guild_id": int(row["guildid"]),
            "guild_name": row["guild_name"],
            "occupied_slots": int(row["occupied_slots"]),
            "total_item_level": int(row["total_item_level"]),
            "populated_slot_average_item_level": float(row["populated_slot_average_item_level"]),
            "weapon_item_level": int(row["weapon_item_level"]),
            "weapon_quality": int(row["weapon_quality"]),
        }
        for row in raw_rows
    ]
    if len(rows) != 80 or len({row["guid"] for row in rows}) != 80:
        raise RuntimeError(f"expected 80 distinct active cohort bots, found {len(rows)} rows")

    guild_counts = Counter(row["guild_id"] for row in rows)
    if any(guild_counts[guild_id] != 40 for guild_id in guild_ids):
        raise RuntimeError(f"expected 40 bots in each guild, found {dict(guild_counts)}")

    guild_names = {row["guild_id"]: row["guild_name"] for row in rows}
    excluded = set(args.exclude_seed)
    seed, checked, perfect_evaluated, balance_score = find_perfect_seed(
        rows, args.start_seed, args.max_seed, excluded, args.batch_size, args.perfect_candidates
    )
    if seed is None:
        raise RuntimeError(f"no perfectly balanced seed found after checking {checked} candidates")
    result = summarize(rows, seed, guild_names)

    fingerprint_source = "\n".join(
        f"{row['guid']}:{row['race']}:{row['class']}:{row['guild_id']}" for row in rows
    ).encode("utf-8")
    output = {
        "guild_ids": guild_ids,
        "roster_size": len(rows),
        "roster_sha256": hashlib.sha256(fingerprint_source).hexdigest(),
        "search": {
            "start_seed": args.start_seed,
            "max_seed": args.max_seed,
            "excluded_seeds": sorted(excluded),
            "checked_candidates": checked,
            "perfect_candidates_evaluated": perfect_evaluated,
            "equipment_balance_score": balance_score,
        },
        **result,
    }
    print(json.dumps(output, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
