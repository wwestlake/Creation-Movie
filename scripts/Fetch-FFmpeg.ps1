param([string]$Destination = (Join-Path $PSScriptRoot '../deps'))
$ErrorActionPreference = 'Stop'
$name = 'ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1'
$url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-18-13-01/$name.zip"
$sha256 = 'eb9346bdb3312d51ce5bfcf78041fd17e3db91f9fff9446d0d758406b85d2a51'
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$archive = Join-Path $Destination "$name.zip"
if (!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha256) { throw 'FFmpeg SDK checksum mismatch; nothing extracted.' }
if (!(Test-Path -LiteralPath (Join-Path $Destination "$name/include"))) { Expand-Archive -LiteralPath $archive -DestinationPath $Destination }
Write-Output (Join-Path $Destination $name)
