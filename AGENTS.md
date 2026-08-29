# Repository instructions

## Build

From this module directory, build the server with:

```powershell
cmake --build D:\wowserver\build --config RelWithDebInfo --target worldserver -- /m:4
```

Do not overwrite or discard unrelated worktree changes. The live module configuration is normally
`D:\wowserver\build\bin\RelWithDebInfo\configs\modules\mod_lazy_questing.conf`; the file under
`conf/` is only the distributable default.

## A/B experiment reports

Use `experiment-logs/REPORT_TEMPLATE.md` for every cohort report and add the finished report to
`experiment-logs/README.md`. Name reports `YYYY-MM-DD-cohort-N-short-comparison.md`. Keep raw
runtime logs out of the repository.

The report must answer, in this order:

1. Did the treatment activate and was assignment/exposure intact?
2. Did it improve total progression, especially XP and time-to-level?
3. Which mechanism changed: questing, combat, travel, loot, gear, deaths, or stalls?
4. Did the result persist over time, across level bands, and without catastrophic class/race tails?
5. What decision follows, and what single uncertainty should the next cohort isolate?

Use the configured run ID to isolate runtime records. Do not analyze every `[LQ]` line in a log
that may contain multiple cohorts or server restarts.

### Collect the live configuration and recorder lines

Set these values for the cohort under review:

```powershell
$serverBin = 'D:\wowserver\build\bin\RelWithDebInfo'
$liveConfig = "$serverBin\configs\modules\mod_lazy_questing.conf"
$playerbotLog = "$serverBin\Playerbots.log"
$runId = 'cohort-N-description'
$guildIds = 'ALLIANCE_GUILD_ID,HORDE_GUILD_ID'
```

Read the exact assignment and recorder settings, then extract only this run:

```powershell
Select-String -Path $liveConfig -Pattern 'LazyQuesting.Experiment|LazyQuesting.FlightRecorder'
rg -n -F "run=$runId" $playerbotLog
```

The relevant records are `[LQ] scheduler`, `[LQ][intent]`, `[LQ][flight]`, and `[LQ][gear]`.
Treat each flight/gear line as one interval, not as a cumulative counter. Split recorder epochs at
server restarts. Exclude startup/shutdown fragments and pair only intervals in which both arms are
present with the expected bot counts. Record every exclusion in the report.

### Collect the live Aquarium roster

Aquarium is authoritative for the current level, online state, location, bags, money, and equipped
item level of online bots. Run it through the loopback SOAP endpoint:

```powershell
$soapUser = Read-Host 'SOAP user'
$soapPassword = Read-Host 'SOAP password' -AsSecureString
$soapCredential = [pscredential]::new($soapUser, $soapPassword)
$soapBody = '<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:AC"><SOAP-ENV:Body><ns1:executeCommand><command>strictbots aquarium roster</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>'
$soapResponse = Invoke-WebRequest -Uri 'http://127.0.0.1:7878/' -Method Post -Credential $soapCredential -AllowUnencryptedAuthentication -ContentType 'text/xml; charset=utf-8' -Body $soapBody
[xml]$soapXml = $soapResponse.Content
$rosterJson = $soapXml.SelectSingleNode("//*[local-name()='result']").InnerText -replace '^ALTBO_JSON\s+', ''
$roster = ($rosterJson.Trim() | ConvertFrom-Json).bots
$roster | Sort-Object level,name | Format-Table guid,name,level,faction,itemLevel,online,area,state
```

The unencrypted-authentication switch is acceptable only for this loopback endpoint. Never commit
SOAP or database credentials.

### Collect durable character endpoints

Resolve the local character-database credentials from `worldserver.conf` and let `mysql` prompt for
the password. Use guild IDs rather than name patterns so retired and similarly named cohorts cannot
leak into the sample:

```powershell
$characterQuery = @"
SELECT c.guid,c.name,c.race,c.class,c.level,c.xp,c.money,c.totaltime,c.online,
       COUNT(q.quest) AS rewarded_quests
FROM guild_member gm
JOIN characters c ON c.guid=gm.guid
LEFT JOIN character_queststatus_rewarded q ON q.guid=c.guid
WHERE gm.guildid IN ($guildIds)
GROUP BY c.guid,c.name,c.race,c.class,c.level,c.xp,c.money,c.totaltime,c.online
ORDER BY c.guid;

SELECT l.character_guid,l.level,l.level_up_at,l.played_since_first_login_seconds,
       l.quests_completed_amount,l.item_level
FROM strict_altbot_levelups l
JOIN guild_member gm ON gm.guid=l.character_guid
WHERE gm.guildid IN ($guildIds)
ORDER BY l.character_guid,l.level;
"@
mysql --host=127.0.0.1 --user=acore --password --database=acore_characters --batch --raw --execute=$characterQuery
```

`characters` may lag behind the in-memory state while a bot is online. Use Aquarium for the live
snapshot and `strict_altbot_levelups` for durable attained-level milestones. Do not label an online
bot a straggler from a stale `characters.level` row without checking both sources.

### Reconstruct experiment arms and verify balance

Fetch `guid`, `race`, and `class` for every cohort bot, then reproduce `GetExperimentBucket` in
`src/LazyQuestingModule.cpp` using the exact configured seed and unsigned 64-bit wraparound. Apply
the configured control and assist-only percentage boundaries. Do not substitute CRC, a language
runtime hash, bot name, or guild/faction for this function.

Before analyzing outcomes, verify all of the following:

- total arm counts match scheduler output;
- each faction is split as intended;
- race and class splits are exact where category counts are even and differ by at most one where
  they are odd, unless the preregistered design says otherwise;
- complete recorder intervals contain the expected bot count in both arms;
- control has no Lazy Questing intents and treatment activation matches its contract.

If assignment or exposure integrity fails, report that first and treat outcome estimates as
exploratory.

### Calculate and interpret results

- Sum event counters over complete paired intervals. Divide by observed bot-hours to report rates.
- Convert activity seconds to percentages within each arm; report treatment-minus-control changes
  in percentage points.
- Show treatment relative to control as `(treatment / control - 1) * 100`. Use `n/a` when the
  control value is zero.
- Analyze first/last windows and same-level bands. A full-run average can hide deterioration.
- Compare live and milestone gear using player level, populated-slot average item level, total
  equipped item-level points, occupied slots, weapon item level, loot gear, quest-reward gear, and
  equip events. Do not infer better gear merely from more completed quests.
- Inspect the worst bots and class/race tails. Repeat the headline estimate without obvious
  catastrophic outliers, but disclose both results.
- For per-bot endpoints, report means/medians and an approximate Welch 95% interval when useful.
  Treat it as diagnostic because bots share a world and are not independent experimental units.
- Evaluate the preregistered promotion gates before choosing a verdict. Do not promote a treatment
  from a short smoke-test interval or a single favorable secondary metric.

The final report should be concise but auditable: preserve the seed, run ID, exposure, exclusions,
raw aggregate totals or rates, important caveats, and the next decision.
