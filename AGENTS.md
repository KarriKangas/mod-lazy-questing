# Repository instructions

## Purpose

Follow DESIGN.md. This repository now builds a character controller around ordinary WoW actions; the previous quest-nudging project is archived. Do not restore the old cohort roadmap as the active plan.

## Boundaries

- Preserve unrelated changes and never commit credentials or raw runtime logs.
- No generated rewards, synthetic power, free resources, teleport recovery, or hidden live-world knowledge.
- Keep execution ownership explicit. Avoid independent controllers fighting over travel or strategy state.
- Keep implementation small and reviewable. Prove a supported gameplay loop before expanding scope.
- Do not modify sibling repositories or live configuration without task authorization covering those changes.
- Do not launch experiments or restart the live server without explicit authorization.

## Build

From this module directory:

```powershell
cmake --build D:\wowserver\build --config RelWithDebInfo --target worldserver -- /m:4
```

The live configuration, when applicable, is under `D:\wowserver\build\bin\RelWithDebInfo\configs\modules`; repository defaults are not live configuration. A running executable may prevent relinking; do not stop it without authorization.

Run checks appropriate to code changes. Documentation-only changes do not require a server build. Never claim a build or runtime validation that did not complete.

## Historical evidence

The ZIP under archive/ preserves the old source, reports, reporting template, and detailed collection instructions, including previously uncommitted work. Keep it immutable. Consult it as evidence; do not compile archived source or treat prior treatments as approved.
