[CmdletBinding()]
param(
    [string]$RunId = 'cohort-10-conservative-assist-vs-control',
    [int]$Seed = 54529878,
    [string]$GuildIds = '40,41',
    [int]$ExpectedBotsPerArm = 40,
    [int]$ExposureSeconds = 25200,
    [int]$ExcludedGuid = 2537,
    [string]$ServerBin = 'D:\wowserver\build\bin\RelWithDebInfo',
    [string]$PriorAggregate = (Join-Path $PSScriptRoot '2026-08-29-cohort-10-epoch-1-checkpoint.json'),
    [string]$SecondEpochBaseline = (Join-Path $PSScriptRoot '2026-08-29-cohort-10-epoch-2-baseline.json'),
    [switch]$StatusOnly
)

$ErrorActionPreference = 'Stop'

$worldserverConf = Join-Path $ServerBin 'configs\worldserver.conf'
$moduleConfig = Join-Path $ServerBin 'configs\modules\mod_lazy_questing.conf'
$playerbotLog = Join-Path $ServerBin 'Playerbots.log'
$durableAnalyzer = Join-Path $PSScriptRoot 'analyze_durable_endpoints.py'
$recorderAnalyzer = Join-Path $PSScriptRoot 'analyze_flight_recorder.py'
$endpointSummarizer = Join-Path $PSScriptRoot 'summarize_cohort_endpoint.py'

foreach ($requiredPath in @(
    $worldserverConf,
    $moduleConfig,
    $playerbotLog,
    $durableAnalyzer,
    $recorderAnalyzer,
    $endpointSummarizer,
    $PriorAggregate,
    $SecondEpochBaseline
)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file does not exist: $requiredPath"
    }
}

$baseline = Get-Content -LiteralPath $SecondEpochBaseline -Raw | ConvertFrom-Json
if ($baseline.run_id -ne $RunId) {
    throw "Second-epoch baseline run ID does not match $RunId"
}
if ($baseline.expected_bots_per_arm -ne $ExpectedBotsPerArm) {
    throw 'Second-epoch baseline count contract does not match the requested capture contract'
}

$worldserver = Get-Process worldserver -ErrorAction Stop | Sort-Object StartTime -Descending | Select-Object -First 1
$expectedStartTime = [DateTimeOffset]$baseline.worldserver.start_time
$actualStartTime = [DateTimeOffset]$worldserver.StartTime
if ([Math]::Abs(($actualStartTime - $expectedStartTime).TotalSeconds) -gt 1) {
    throw (
        "Worldserver epoch changed: expected start {0}, found {1}. Preserve the new epoch and " +
        'establish a new read-only exposure baseline before continuing.' -f
        $expectedStartTime.ToString('o'), $actualStartTime.ToString('o')
    )
}

$summaryArguments = @(
    $durableAnalyzer,
    '--worldserver-conf', $worldserverConf,
    '--guild-ids', $GuildIds,
    '--seed', [string]$Seed,
    '--exposure-seconds', [string]$ExposureSeconds,
    '--summary-only'
)
$summaryText = (& python @summaryArguments | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Durable endpoint status failed with exit code $LASTEXITCODE"
}
$summary = $summaryText | ConvertFrom-Json

$recorderStatusArguments = @(
    $recorderAnalyzer,
    '--log', $playerbotLog,
    '--run-id', $RunId,
    '--expected-bots', [string]$ExpectedBotsPerArm,
    '--exclude-first', '1'
)
$recorderStatusText = (& python @recorderStatusArguments | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Recorder endpoint status failed with exit code $LASTEXITCODE"
}
$recorderStatus = $recorderStatusText | ConvertFrom-Json
$additionalCompleteIntervals = [Math]::Max(0, [int]$recorderStatus.retained_intervals - [int]$baseline.retained_intervals)
$additionalControlSecondsPerBot = [Math]::Max(
    0,
    ([double]$recorderStatus.full.arms.control.bot_seconds - [double]$baseline.observed_bot_seconds.control) /
        $ExpectedBotsPerArm
)
$additionalAssistSecondsPerBot = [Math]::Max(
    0,
    ([double]$recorderStatus.full.arms.'assist-only'.bot_seconds - [double]$baseline.observed_bot_seconds.'assist-only') /
        $ExpectedBotsPerArm
)
$additionalObservedSecondsPerBot = [Math]::Floor(
    [Math]::Min($additionalControlSecondsPerBot, $additionalAssistSecondsPerBot)
)
$recorderAdvancedFloor = (
    [int]$baseline.durable_minimum_seconds_without_zurmos +
    $additionalObservedSecondsPerBot
)
$durableFloor = [int]$summary.exposure.without_zurmos_observed_floor.minimum_seconds
$effectiveFloor = [Math]::Max($durableFloor, $recorderAdvancedFloor)

$status = [ordered]@{
    checked_at = (Get-Date).ToString('o')
    run_id = $RunId
    target_seconds = $ExposureSeconds
    excluded_guid_for_readiness = $ExcludedGuid
    worldserver = [ordered]@{
        pid = $worldserver.Id
        start_time = $worldserver.StartTime.ToString('o')
        responding = $worldserver.Responding
    }
    live_log = [ordered]@{
        path = $playerbotLog
        bytes = (Get-Item -LiteralPath $playerbotLog).Length
        last_write_time = (Get-Item -LiteralPath $playerbotLog).LastWriteTime.ToString('o')
    }
    endpoint_ready_without_excluded_guid = ($effectiveFloor -ge $ExposureSeconds)
    observed_floor_without_excluded_guid = $summary.exposure.without_zurmos_observed_floor
    recorder_validated_exposure = [ordered]@{
        baseline_checked_at = $baseline.checked_at
        baseline_minimum_seconds = $baseline.durable_minimum_seconds_without_zurmos
        baseline_retained_intervals = $baseline.retained_intervals
        current_retained_intervals = $recorderStatus.retained_intervals
        additional_complete_intervals = $additionalCompleteIntervals
        additional_control_seconds_per_bot = $additionalControlSecondsPerBot
        additional_assist_seconds_per_bot = $additionalAssistSecondsPerBot
        additional_observed_seconds_per_bot = $additionalObservedSecondsPerBot
        recorder_advanced_floor_seconds = $recorderAdvancedFloor
        durable_floor_seconds = $durableFloor
        effective_floor_seconds = $effectiveFloor
        remaining_seconds = [Math]::Max(0, $ExposureSeconds - $effectiveFloor)
        current_exclusions = $recorderStatus.exclusions
    }
    assignment_counts = $summary.assignment_counts
    online_count_saved = $summary.online_count_saved
    attained_level_distribution = $summary.attained_level_distribution
    zurmos_latest_milestone = $summary.zurmos.latest_milestone
    authority_caveat = $summary.exposure.authority_caveat
}

if ($StatusOnly -or -not $status.endpoint_ready_without_excluded_guid) {
    $status | ConvertTo-Json -Depth 8
    if (-not $StatusOnly) {
        exit 2
    }
    exit 0
}

$configurationLines = @(
    Select-String -Path $moduleConfig -Pattern 'LazyQuesting.Experiment|LazyQuesting.FlightRecorder' |
        ForEach-Object { $_.Line.Trim() }
)
$expectedConfiguration = @(
    "LazyQuesting.Experiment.RunId = $RunId",
    "LazyQuesting.Experiment.Seed = $Seed",
    'LazyQuesting.Experiment.ControlPercent = 50',
    'LazyQuesting.Experiment.AssistOnlyPercent = 50',
    'LazyQuesting.FlightRecorder.Enable = 1'
)
foreach ($expectedLine in $expectedConfiguration) {
    if ($configurationLines -notcontains $expectedLine) {
        throw "Live experiment configuration does not contain expected line: $expectedLine"
    }
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$captureDirectory = Join-Path ([IO.Path]::GetTempPath()) "lazy-questing-$RunId-$timestamp"
$resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$resolvedCapture = [IO.Path]::GetFullPath($captureDirectory)
if (-not $resolvedCapture.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create capture outside the temporary directory: $resolvedCapture"
}
[void](New-Item -ItemType Directory -Path $resolvedCapture)

$snapshotPath = Join-Path $resolvedCapture 'Playerbots.second-epoch.snapshot.log'
$exactRunPath = Join-Path $resolvedCapture 'cohort-10.exact-run.second-epoch.log'
$recorderAnalysisPath = Join-Path $resolvedCapture 'recorder-analysis.json'
$durableAnalysisPath = Join-Path $resolvedCapture 'durable-endpoints.json'
$reportSummaryPath = Join-Path $resolvedCapture 'report-summary.json'
$statusPath = Join-Path $resolvedCapture 'endpoint-status.json'
$manifestPath = Join-Path $resolvedCapture 'manifest.json'

Copy-Item -LiteralPath $playerbotLog -Destination $snapshotPath
$exactRunLines = @(
    Select-String -LiteralPath $snapshotPath -SimpleMatch "run=$RunId" |
        ForEach-Object { $_.Line }
)
if (-not $exactRunLines.Count) {
    throw "Snapshot contains no lines for exact run ID $RunId"
}
$exactRunLines | Set-Content -LiteralPath $exactRunPath -Encoding utf8

$recorderArguments = @(
    $recorderAnalyzer,
    '--log', $exactRunPath,
    '--run-id', $RunId,
    '--expected-bots', [string]$ExpectedBotsPerArm,
    '--exclude-first', '1',
    '--prior-aggregate', $PriorAggregate
)
$recorderText = (& python @recorderArguments | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Recorder analysis failed with exit code $LASTEXITCODE"
}
$recorderText | Set-Content -LiteralPath $recorderAnalysisPath -Encoding utf8

$durableArguments = @(
    $durableAnalyzer,
    '--worldserver-conf', $worldserverConf,
    '--guild-ids', $GuildIds,
    '--seed', [string]$Seed,
    '--exposure-seconds', [string]$ExposureSeconds
)
$durableText = (& python @durableArguments | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Durable endpoint analysis failed with exit code $LASTEXITCODE"
}
$durableText | Set-Content -LiteralPath $durableAnalysisPath -Encoding utf8
$reportSummaryArguments = @(
    $endpointSummarizer,
    '--recorder', $recorderAnalysisPath,
    '--durable', $durableAnalysisPath,
    '--prior-aggregate', $PriorAggregate
)
$reportSummaryText = (& python @reportSummaryArguments | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Endpoint gate summary failed with exit code $LASTEXITCODE"
}
$reportSummaryText | Set-Content -LiteralPath $reportSummaryPath -Encoding utf8
$status | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $statusPath -Encoding utf8

$manifest = [ordered]@{
    captured_at = (Get-Date).ToString('o')
    run_id = $RunId
    seed = $Seed
    guild_ids = $GuildIds
    exposure_seconds = $ExposureSeconds
    excluded_guid_for_readiness = $ExcludedGuid
    worldserver = [ordered]@{
        pid = $worldserver.Id
        start_time = $worldserver.StartTime.ToString('o')
        responding = $worldserver.Responding
    }
    live_configuration = $configurationLines
    exact_run_line_count = $exactRunLines.Count
    files = [ordered]@{}
    caveats = @(
        'Raw logs remain outside the repository.',
        'The first recorder epoch raw log was reset externally; its captured aggregate is merged through --prior-aggregate.',
        'Aquarium live roster is collected separately when loopback SOAP credentials are available.'
    )
}
foreach ($artifact in @(
    $snapshotPath,
    $exactRunPath,
    $recorderAnalysisPath,
    $durableAnalysisPath,
    $reportSummaryPath,
    $statusPath
)) {
    $item = Get-Item -LiteralPath $artifact
    $manifest.files[$item.Name] = [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8

[ordered]@{
    endpoint_captured = $true
    capture_directory = $resolvedCapture
    manifest = $manifestPath
    exact_run_line_count = $exactRunLines.Count
    recorder_retained_intervals = (($recorderText | ConvertFrom-Json).retained_intervals)
    combined_retained_intervals = (($recorderText | ConvertFrom-Json).combined_full.intervals)
    all_promotion_gates_pass = (($reportSummaryText | ConvertFrom-Json).all_gates_pass)
} | ConvertTo-Json -Depth 5
