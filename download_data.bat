@echo off
set BASE=https://raw.githubusercontent.com/jpatokal/openflights/master/data
if not exist data mkdir data
echo Downloading airports.dat...
curl -L -o data\airports.dat %BASE%/airports.dat
echo Downloading airlines.dat...
curl -L -o data\airlines.dat %BASE%/airlines.dat
echo Downloading routes.dat...
curl -L -o data\routes.dat %BASE%/routes.dat
echo Done. Files in data\
