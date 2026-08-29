# Lazy Questing experiment log

This directory contains durable summaries of live Lazy Questing cohort experiments. Each report records the cohort, assignment configuration, observation window, headline measurements, known caveats, and the decision taken from the result.

## Experiments

| Date | Cohort | Comparison | Result |
| --- | --- | --- | --- |
| [2026-08-27](2026-08-27-cohort-8-current-vs-control.md) | Cohort 8 | full current behavior vs control | Full treatment lost on progression and economy despite reducing idle time and deaths. |
| [2026-08-28](2026-08-28-cohort-9-assist-only-vs-control.md) | Cohort 9 | assist-only vs control | Quest-specific outcomes improved modestly, but total XP and combat throughput remained lower, especially late in the run. |
| [2026-08-29](2026-08-29-cohort-10-conservative-assist-vs-control.md) | Cohort 10 | conservative local assist vs control | Running; exact faction/race/class balance with stricter non-interference and expanded telemetry. |

Raw runtime output is operational data and may be overwritten when the worldserver restarts or reloads its logging configuration. These reports are the repository-owned analytical record and should be sufficient to reproduce the interpretation without relying on a rotating server log.

Start new reports from [REPORT_TEMPLATE.md](REPORT_TEMPLATE.md). The repository-level `AGENTS.md`
contains the canonical collection commands, source-precedence rules, calculations, and integrity
checks used for these comparisons.
