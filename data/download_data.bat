@echo off
cd /d "%~dp0"
echo Downloading OpenFlights data...
powershell -NoProfile -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat' -OutFile 'airports.dat' -UseBasicParsing }"
powershell -NoProfile -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat' -OutFile 'airlines.dat' -UseBasicParsing }"
powershell -NoProfile -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat' -OutFile 'routes.dat' -UseBasicParsing }"
echo Done. Check airports.dat, airlines.dat, routes.dat.
pause
