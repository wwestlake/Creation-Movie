param(
    [Parameter(Mandatory=$true)][string]$VfsRoot,
    [Parameter(Mandatory=$true)][string]$LanAddress,
    [Parameter(Mandatory=$true)][string]$Python,
    [switch]$PhysicalPhone
)
$ErrorActionPreference='Stop'
$movieRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output=Join-Path $movieRoot $(if($PhysicalPhone){'build/multitrack-phone-check'}else{'build/multitrack-viewer-check'})
New-Item -ItemType Directory -Force $output|Out-Null
$env:TEMP=Join-Path $movieRoot 'build/temp';$env:TMP=$env:TEMP
$checks=[System.Collections.Generic.List[string]]::new()
$sender=$null;$fixture=$null;$original='';$project='';$failure='';$takeStarted=$false
$heartbeat=Get-Content (Join-Path $VfsRoot 'VfsHeartbeat.json') -Raw|ConvertFrom-Json
$agent=Invoke-RestMethod "http://127.0.0.1:$($heartbeat.httpPort)/suite/entry?path=suite/agents/movie.json" -TimeoutSec 5
$headers=@{Authorization="Bearer $($agent.token)"}
function Command([string]$name,[hashtable]$arguments=@{}) {
    $ack=Invoke-RestMethod "$($agent.baseUrl)/v1/commands/$name" -Headers $headers -Method Post -ContentType application/json -Body ($arguments|ConvertTo-Json -Depth 20 -Compress) -TimeoutSec 10
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    do {
        $result=Invoke-RestMethod "$($agent.baseUrl)/v1/requests/$($ack.requestId)" -Headers $headers -TimeoutSec 10
        if($result.status -eq 'failed'){throw "$name failed: $($result.error)"}
        if($result.status -eq 'completed'){return $result.details.data}
        if([DateTime]::UtcNow -gt $deadline){throw "$name timed out"}
        Start-Sleep -Milliseconds 100
    }while($true)
}
function Job {
    $deadline=[DateTime]::UtcNow.AddSeconds(60)
    do {
        $job=Invoke-RestMethod "$($agent.baseUrl)/v1/app/jobs" -Headers $headers -TimeoutSec 10
        if($job.status -in @('failed','cancelled')){throw "Project job failed: $($job.error)"}
        if($job.status -eq 'completed'){return}
        if([DateTime]::UtcNow -gt $deadline){throw 'Project job timed out'}
        Start-Sleep -Milliseconds 200
    }while($true)
}
function Get-MovieSequence {
    $sequence=Invoke-RestMethod "$($agent.baseUrl)/v1/app/sequence" -Headers $headers -TimeoutSec 10
    $sequence.media=@($sequence.media|Sort-Object id,versionId)
    return $sequence
}
function Assert([bool]$value,[string]$reason){if(!$value){throw $reason}}
function Snapshot([string]$name) {
    $snapshot=Command 'movie.debug.viewer.image'
    Assert ([bool]$snapshot.showing) 'Central Viewer is not actually showing in the mounted workspace'
    [IO.File]::WriteAllBytes((Join-Path $output "$name.png"),[Convert]::FromBase64String($snapshot.png))
}
try {
    $initial=Command 'movie.state.get';$original=$initial.projectId
    if($original){$null=Command 'movie.project.save';Job}
    $null=Command 'movie.project.new' @{name=('Multitrack acquisition check '+[guid]::NewGuid());width=640;height=360};Job
    $project=(Command 'movie.state.get').projectId
    $fixtureTitle='MovieMultitrackWindow-'+[guid]::NewGuid()
    $fixture=Start-Process -FilePath (Join-Path $movieRoot 'build/Debug/MovieVisualCaptureTests.exe') -ArgumentList @('--fixture',$fixtureTitle) -PassThru
    if($PhysicalPhone){Assert ((Command 'movie.webcam.status').state -eq 'connected') 'Connect the phone before running this check'}
    else {
        $invitation=Command 'movie.webcam.invite' @{address=$LanAddress;port=8765}
        $invitation|ConvertTo-Json -Compress|Set-Content (Join-Path $output 'invitation.private.json')
        if(Test-Path (Join-Path $output 'stop-sender')){Remove-Item -LiteralPath (Join-Path $output 'stop-sender')}
        $sender=Start-Process -FilePath $Python -ArgumentList @('-B',(Join-Path $PSScriptRoot 'run-native.py'),'--invitation-file',(Join-Path $output 'invitation.private.json'),'--output',$output,'--bind',$LanAddress) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'sender.log') -RedirectStandardError (Join-Path $output 'sender-error.log')
    }
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    do {
        $sources=@(Command 'movie.capture.sources');$remote=Command 'movie.webcam.status'
        $cameras=@($sources|Where-Object {$_.kind -eq 'camera' -and $_.id -ne 'webcam:video'})
        $camera=$cameras|Where-Object name -match 'C920|Logitech|HD Pro Webcam'|Select-Object -First 1
        if(!$camera){$camera=$cameras|Where-Object name -notmatch 'Virtual|Animaze|OBS'|Select-Object -First 1}
        $screen=$sources|Where-Object kind -eq 'display'|Select-Object -First 1
        $window=$sources|Where-Object {$_.kind -eq 'window' -and $_.name.Contains($fixtureTitle)}|Select-Object -First 1
        if($camera -and $screen -and $window -and ($sources|Where-Object id -eq 'webcam:audio') -and $remote.clockSamples -ge 3){break}
        if([DateTime]::UtcNow -gt $deadline){throw 'Need a native webcam, an Android/WebRTC camera+microphone and a display'}
        Start-Sleep -Milliseconds 200
    }while($true)
    $ids=@($camera.id,'webcam:video',$screen.id,'webcam:audio',$window.id)
    $expected=$ids.Count
    $tracks=@()
    foreach($id in $ids) {
        $track=Command 'movie.capture.add' @{sourceId=$id}
        $tracks+=,$track
        $null=Command 'movie.track.preferences' @{trackId=$track.id;armed=$false;monitoring=$false}
    }
    # A disabled input with a legacy Arm flag must not silently join this take.
    $null=Command 'movie.track.create' @{type='video';name='Disabled input';source=$tracks[0].source}
    $disabled=(Get-MovieSequence).tracks|Select-Object -Last 1
    $null=Command 'movie.track.preferences' @{trackId=$disabled.id;armed=$true}
    $null=Command 'movie.track.enabled' @{trackId=$disabled.id;enabled=$false}
    $ready=Command 'movie.recording.readiness'
    Assert ([bool]$ready.ready -and $ready.tracks.Count -eq $expected) 'Enabled input membership still depends on arming or includes a disabled input'
    $checks.Add('Five enabled camera/phone/display/window/audio inputs participate with legacy Arm/Monitor off; disabled input is excluded')
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        $viewer=Command 'movie.capture.monitor' @{trackId=$tracks[0].id}
        if($viewer.hasImage){break}
        if([DateTime]::UtcNow -gt $deadline){throw 'Native camera did not appear in Central Viewer'}
        Start-Sleep -Milliseconds 200
    }while($true)
    Snapshot 'central-native-camera'
    $viewer=Command 'movie.capture.monitor' @{trackId=$tracks[1].id}
    Assert ([bool]$viewer.hasImage -and $viewer.sourceId -eq 'webcam:video') 'Android source is absent from Central Viewer'
    Snapshot 'central-android-camera'
    $before=(Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress)
    $null=Command 'movie.recording.start' @{position=1.25};$takeStarted=$true
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do {
        $take=Command 'movie.recording.status'
        if($take.state -eq 'recording'){break}
        if($take.state -eq 'failed' -or [DateTime]::UtcNow -gt $deadline){throw "Take did not start: $($take.error)"}
        Start-Sleep -Milliseconds 200
    }while($true)
    $switches=[System.Collections.Generic.List[object]]::new()
    foreach($edit in @('movie.track.enabled','movie.timeline.undo','movie.timeline.redo')) {
        $rejected=$false
        try {$null=Command $edit @{trackId=$tracks[0].id;enabled=$false}}catch{$rejected=$true}
        Assert $rejected "Input mutation was accepted during recording: $edit"
        Assert ((Command 'movie.recording.status').state -eq 'recording') 'Rejected input edit interrupted recording'
        Assert ((Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress) -eq $before) 'Rejected input edit changed the capture document'
    }
    foreach($id in @($tracks[0].id,$tracks[1].id,$tracks[2].id,$tracks[3].id,$tracks[4].id,'',$disabled.id,$tracks[1].id)) {
        $viewer=Command 'movie.capture.monitor' @{trackId=$id}
        Assert ((Command 'movie.recording.status').state -eq 'recording') 'Viewer selection interrupted recording'
        Assert ((Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress) -eq $before) 'Viewer selection changed document/source configuration'
        $switches.Add($viewer)
        Start-Sleep -Milliseconds 600
    }
    Snapshot 'central-while-recording'
    Start-Sleep -Seconds 2
    $during=Command 'movie.recording.status'
    Assert ($during.tracks.Count -eq $expected) 'Monitor switching changed the set of recording subscriptions'
    $checks.Add('Camera/phone/display/window/audio/timeline monitoring leaves all recording subscriptions and the document unchanged')
    $null=Command 'movie.recording.stop'
    $deadline=[DateTime]::UtcNow.AddSeconds(60)
    do {
        $take=Command 'movie.recording.status'
        if($take.state -in @('completed','partial','failed') -and !$take.pendingPublication){break}
        if([DateTime]::UtcNow -gt $deadline){throw 'Multitrack take did not finalize'}
        Start-Sleep -Milliseconds 200
    }while($true)
    Assert ($take.state -eq 'completed' -and $take.results.tracks.Count -eq $expected) 'Take did not produce one independent usable recording for every enabled input'
    Job
    foreach($recorded in $take.results.tracks) {
        Assert ([bool]$recorded.usable -and !$recorded.error) "Unusable recording: $($recorded.sourceId): $($recorded.error)"
        Assert ($recorded.info.duration -gt 4 -and $recorded.diagnostics.packets -gt 5) "Input did not record continuously: $($recorded.sourceId)"
        $offset=($recorded.diagnostics.firstHostTime100ns-$take.origin100ns)/10000000.0
        Assert ([math]::Abs($recorded.startSeconds-1.25-$offset) -lt 0.000001) 'Track placement lost the common recording epoch'
    }
    $sequence=Get-MovieSequence
    Assert (@($sequence.clips|Where-Object id -like 'capture-*').Count -eq $expected) 'Independent recordings were not placed on their own editing tracks'
    $checks.Add('Five continuous separate recordings, including a resizing window, retain source identity, original clock diagnostics and common timeline offsets; no composite is generated')
    $media=$sequence.media|ConvertTo-Json -Depth 20 -Compress
    $clip=$sequence.clips|Where-Object id -like 'capture-*'|Select-Object -First 1
    $null=Command 'movie.timeline.trim' @{clipId=$clip.id;sourceIn=0.5;duration=1.5}
    Assert (((Get-MovieSequence).media|ConvertTo-Json -Depth 20 -Compress) -eq $media) 'Editing a placement changed original recorded media'
    $null=Command 'movie.timeline.undo'
    $null=Command 'movie.project.save';Job
    $saved=Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress
    $null=Command 'movie.project.open' @{projectId=$project};Job
    Assert ((Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress) -eq $saved) 'Save/reopen changed synchronized capture tracks or original media'
    $null=Command 'movie.capture.monitor' @{trackId=''}
    Snapshot 'central-timeline-review'
    $checks.Add('Nondestructive trim/undo and save/reopen preserve originals and synchronized track ownership; timeline review remains available')
    $take|ConvertTo-Json -Depth 30|Set-Content (Join-Path $output 'take-results.json')
    $switches|ConvertTo-Json -Depth 20|Set-Content (Join-Path $output 'viewer-switches.json')
}catch{$failure=$_.Exception.Message;throw}
finally {
    if($takeStarted){try{$null=Command 'movie.recording.stop'}catch{}}
    if(!$PhysicalPhone){try{$null=Command 'movie.webcam.stop'}catch{}}
    if($sender){Set-Content (Join-Path $output 'stop-sender') 'stop';$sender.WaitForExit(10000)|Out-Null}
    if($fixture -and !$fixture.HasExited){$null=$fixture.CloseMainWindow();$null=$fixture.WaitForExit(5000)}
    if(Test-Path (Join-Path $output 'invitation.private.json')){Remove-Item -LiteralPath (Join-Path $output 'invitation.private.json')}
    if($original -and (!$PhysicalPhone -or $failure)){try{$null=Command 'movie.project.open' @{projectId=$original;discardChanges=$true};Job}catch{}}
    elseif($PhysicalPhone -and !$failure){try{
        $null=Command 'movie.track.enabled' @{trackId=$tracks[4].id;enabled=$false}
        $null=Command 'movie.capture.monitor' @{trackId=$tracks[1].id}
        $null=Command 'movie.project.save';Job
    }catch{}}
    @{checks=$checks;projectId=$project;error=$failure;physicalAndroid=[bool]$PhysicalPhone;syntheticAndroid=(!$PhysicalPhone)}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $output 'results.json')
}
Write-Output "Multitrack viewer workflow passed $($checks.Count) groups (physical Android: $PhysicalPhone)"
