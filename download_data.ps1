# PowerShell: download OpenFlights data into data/
$base = "https://raw.githubusercontent.com/jpatokal/openflights/master/data"
if (-not (Test-Path data)) { New-Item -ItemType Directory -Path data }
Invoke-WebRequest -Uri "$base/airports.dat" -OutFile "data/airports.dat" -UseBasicParsing
Invoke-WebRequest -Uri "$base/airlines.dat" -OutFile "data/airlines.dat" -UseBasicParsing
Invoke-WebRequest -Uri "$base/routes.dat" -OutFile "data/routes.dat" -UseBasicParsing
Write-Host "Done. Files in data/"
