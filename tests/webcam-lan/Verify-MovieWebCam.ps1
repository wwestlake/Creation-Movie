param(
    [Parameter(Mandatory=$true)][string]$VfsRoot,
    [Parameter(Mandatory=$true)][string]$LanAddress,
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$PythonPackages
)
$ErrorActionPreference='Stop'
$movieRoot=Resolve-Path (Join-Path $PSScriptRoot '../..')
$output=Join-Path $movieRoot 'build/webcam-movie-test'
New-Item -ItemType Directory -Force $output | Out-Null
$env:TEMP=Join-Path $movieRoot 'build/temp';$env:TMP=$env:TEMP
$env:PYTHONDONTWRITEBYTECODE='1';$env:PYTHONPATH=$PythonPackages
$checks=[System.Collections.Generic.List[string]]::new()
$sender=$null;$original='';$project='';$failure=''
$discoveryDeadline=[DateTime]::UtcNow.AddSeconds(45)
do{
    try{
        $heartbeat=Get-Content (Join-Path $VfsRoot 'VfsHeartbeat.json') -Raw|ConvertFrom-Json
        $agent=Invoke-RestMethod "http://127.0.0.1:$($heartbeat.httpPort)/suite/entry?path=suite/agents/movie.json" -TimeoutSec 5
        $headers=@{Authorization="Bearer $($agent.token)"}
        $null=Invoke-RestMethod "$($agent.baseUrl)/v1/app/state" -Headers $headers -TimeoutSec 5
        break
    }catch{if([DateTime]::UtcNow -gt $discoveryDeadline){throw};Start-Sleep -Milliseconds 200}
}while($true)
function Command([string]$name,[hashtable]$arguments=@{}) {
    $ack=Invoke-RestMethod "$($agent.baseUrl)/v1/commands/$name" -Headers $headers -Method Post -ContentType application/json -Body ($arguments|ConvertTo-Json -Depth 20 -Compress) -TimeoutSec 10
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do{
        $result=Invoke-RestMethod "$($agent.baseUrl)/v1/requests/$($ack.requestId)" -Headers $headers -TimeoutSec 10
        if($result.status -eq 'failed'){throw "$name failed: $($result.error)"}
        if($result.status -eq 'completed'){return $result.details.data}
        if([DateTime]::UtcNow -gt $deadline){throw "$name timed out"}
        Start-Sleep -Milliseconds 100
    }while($true)
}
function Job {
    $deadline=[DateTime]::UtcNow.AddSeconds(60)
    do{$result=Invoke-RestMethod "$($agent.baseUrl)/v1/app/jobs" -Headers $headers -TimeoutSec 10
        if($result.status -eq 'failed' -or $result.status -eq 'cancelled'){throw "Project job failed: $($result.error)"}
        if($result.status -eq 'completed'){return}
        if([DateTime]::UtcNow -gt $deadline){throw 'Project job timed out'}
        Start-Sleep -Milliseconds 200
    }while($true)
}
function Get-MovieSequence {Invoke-RestMethod "$($agent.baseUrl)/v1/app/sequence" -Headers $headers -TimeoutSec 10}
function Assert([bool]$ok,[string]$reason){if(!$ok){throw $reason}}
try {
    $original=(Invoke-RestMethod "$($agent.baseUrl)/v1/app/state" -Headers $headers -TimeoutSec 10).projectId
    if($original){$null=Command 'movie.project.save';Job}
    $null=Command 'movie.project.new' @{name=('WebCam synthetic Order 3 '+[guid]::NewGuid());width=320;height=240};Job
    $project=(Invoke-RestMethod "$($agent.baseUrl)/v1/app/state" -Headers $headers -TimeoutSec 10).projectId
    $invitation=Command 'movie.webcam.invite' @{address=$LanAddress;port=8765}
    $invitation|ConvertTo-Json -Compress|Set-Content (Join-Path $output 'invitation.private.json')
    if(Test-Path (Join-Path $output 'stop-sender')){Remove-Item -LiteralPath (Join-Path $output 'stop-sender')}
    $sender=Start-Process -FilePath $Python -ArgumentList @((Join-Path $PSScriptRoot 'run-native.py'),'--invitation-file',(Join-Path $output 'invitation.private.json'),'--output',$output,'--bind',$LanAddress) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'sender.log') -RedirectStandardError (Join-Path $output 'sender-error.log')
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do{$sources=Command 'movie.capture.sources';$timing=Command 'movie.webcam.status'
        if(($sources|Where-Object {$_.id -like 'webcam:*'}).Count -eq 2 -and $timing.clockSamples -ge 3){break}
        if([DateTime]::UtcNow -gt $deadline){throw 'WebCam source/timing discovery timed out'}
        Start-Sleep -Milliseconds 200
    }while($true)
    $checks.Add('Native receiver exposes synthetic camera and microphone in Movie source catalog')
    foreach($spec in @(@('video','WebCam video A','webcam:video','camera'),@('video','WebCam video B','webcam:video','camera'),@('audio','WebCam audio','webcam:audio','microphone'))){
        $null=Command 'movie.track.create' @{type=$spec[0];name=$spec[1]}
        $track=(Get-MovieSequence).tracks|Where-Object {$_.name -eq $spec[1]}|Select-Object -First 1
        $format=($sources|Where-Object {$_.id -eq $spec[2]}).formats[0]
        $null=Command 'movie.track.source' @{trackId=$track.id;source=@{category=$spec[3];identity=$spec[2];settings=$format}}
        $null=Command 'movie.track.preferences' @{trackId=$track.id;armed=$true;monitoring=$false}
    }
    $null=Command 'movie.recording.start' @{position=1.25}
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do{$take=Command 'movie.recording.status';if($take.state -eq 'recording'){break};if($take.state -eq 'failed' -or [DateTime]::UtcNow -gt $deadline){throw "Recording did not start: $($take.error)"};Start-Sleep -Milliseconds 200}while($true)
    Start-Sleep -Seconds 3
    $null=Command 'movie.recording.stop'
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do{$take=Command 'movie.recording.status';if($take.state -in @('completed','partial','failed') -and !$take.pendingPublication){break};if([DateTime]::UtcNow -gt $deadline){throw 'Recording did not finalize'};Start-Sleep -Milliseconds 200}while($true)
    Assert ($take.results.tracks.Count -eq 3) 'Expected three independently recorded outputs'
    foreach($recorded in $take.results.tracks){Assert ([bool]$recorded.usable) "Unusable WebCam recording: $($recorded.error)"}
    $checks.Add('Existing coordinator records two shared video subscriptions plus audio to ordinary VFS media')
    $sequence=Get-MovieSequence;$clips=@($sequence.clips|Where-Object {$_.id -like 'capture-*'})
    Assert ($clips.Count -eq 3) 'Expected track-owned recording placements'
    $null=Command 'movie.timeline.undo';Assert (@((Get-MovieSequence).clips|Where-Object {$_.id -like 'capture-*'}).Count -eq 0) 'Undo did not remove recording placements'
    $null=Command 'movie.timeline.redo';Assert (@((Get-MovieSequence).clips|Where-Object {$_.id -like 'capture-*'}).Count -eq 3) 'Redo did not restore recording placements'
    $checks.Add('Recording publication participates in normal undo/redo')
    $null=Command 'movie.project.save';Job
    $before=(Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress)
    $null=Command 'movie.project.open' @{projectId=$project};Job
    Assert ((Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress) -eq $before) 'Save/reopen changed capture document'
    $checks.Add('Save/reopen preserves captured media, assignments and precise placements')
    $take|ConvertTo-Json -Depth 20|Set-Content (Join-Path $output 'take-results.json')
    $previousGeneration=$invitation.session
    $null=Command 'movie.webcam.stop';Set-Content (Join-Path $output 'stop-sender') 'stop'
    $sender.WaitForExit(10000)|Out-Null
    Assert $sender.HasExited 'First synthetic sender did not stop'
    $invitation=Command 'movie.webcam.invite' @{address=$LanAddress;port=8765}
    Assert ($invitation.session -ne $previousGeneration) 'Reconnect reused the old connection generation'
    $invitation|ConvertTo-Json -Compress|Set-Content (Join-Path $output 'invitation.private.json')
    Remove-Item -LiteralPath (Join-Path $output 'stop-sender')
    $sender=Start-Process -FilePath $Python -ArgumentList @((Join-Path $PSScriptRoot 'run-native.py'),'--invitation-file',(Join-Path $output 'invitation.private.json'),'--output',$output,'--bind',$LanAddress) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'reconnect-sender.log') -RedirectStandardError (Join-Path $output 'reconnect-sender-error.log')
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do{$timing=Command 'movie.webcam.status';if($timing.localTransportVerified -and $timing.videoClockAnchor -and $timing.audioClockAnchor -and $timing.clockSamples -ge 3){break};if([DateTime]::UtcNow -gt $deadline){throw 'Fresh authenticated generation did not become timing-ready'};Start-Sleep -Milliseconds 200}while($true)
    Assert ((Get-MovieSequence|ConvertTo-Json -Depth 30 -Compress) -eq $before) 'Reconnect altered existing recorded media or assignments'
    $checks.Add('Stop and fresh authenticated reconnect restore timing without changing saved media or track assignments')
}catch{$failure=$_.Exception.Message;throw}
finally{
    try{$null=Command 'movie.recording.stop';$until=[DateTime]::UtcNow.AddSeconds(5);do{$finalTake=Command 'movie.recording.status';if($finalTake.state -notin @('preparing','prepared','recording','stopping')){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)}catch{}
    try{$null=Command 'movie.webcam.stop'}catch{}
    Set-Content (Join-Path $output 'stop-sender') 'stop'
    if($sender){$sender.WaitForExit(10000)|Out-Null}
    if(Test-Path (Join-Path $output 'invitation.private.json')){Remove-Item -LiteralPath (Join-Path $output 'invitation.private.json')}
    if($original){try{$null=Command 'movie.project.open' @{projectId=$original;discardChanges=$true};Job}catch{}}
    @{checks=$checks;projectId=$project;error=$failure;synthetic=$true;physicalAndroid=$false}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $output 'movie-results.json')
}
Write-Output "Movie synthetic WebCam workflow passed $($checks.Count) groups"
