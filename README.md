# Air Travel Database – Honors Capstone Project

C++ web application that front-ends **OpenFlights** data (airports, airlines, routes). Built with the **Crow** framework and designed for the CIS 22CH Vibe Coding Capstone.

## Features

- **Port 8080** – Web server runs locally on port 8080.
- **Airports** – Search by name, city, or IATA/ICAO code.
- **Airlines** – List and search airlines.
- **Queries**
  - Airlines that fly into a given airport (e.g. SFO).
  - Top N cities by route count for an airline (e.g. American Airlines AA).
  - Flight distance between two airports (e.g. SFO–ORD) using GPS coordinates.

## Prerequisites

- **C++17** compiler (e.g. Visual Studio 2019+, or GCC/Clang on Linux/Mac).
- **CMake** 3.16+.
- **vcpkg** (for Crow): [vcpkg installation](https://vcpkg.io/en/getting-started.html).

## Build (Windows with vcpkg)

1. **Install vcpkg** (if needed):
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Configure and build** (from project root; use your vcpkg path):
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build . --config Release
   ```

   Or with vcpkg manifest (vcpkg in PATH or as toolchain):
   ```bash
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ```

3. **Download data** (required before running):
   - **PowerShell:** `.\download_data.ps1`
   - **Batch:** `download_data.bat` (needs `curl`).
   - Or manually download from [OpenFlights data](https://github.com/jpatokal/openflights/tree/master/data) and place `airports.dat`, `airlines.dat`, `routes.dat` in the `data/` folder.

4. **Run** (from project root so `data/` is found, or copy `data/` next to the executable):
   ```bash
   .\build\Release\air_travel_app.exe
   ```
   Or from `build` directory (data is copied there by CMake):
   ```bash
   cd build
   .\Release\air_travel_app.exe
   ```

5. **Test in browser:** open **http://localhost:8080**

## Before submitting (Capstone)

- **Get ID (2.3):** In `src/main.cpp` search for `/api/getid` and set `studentId` and `name` in that handler to your De Anza student ID and full name (see the `YOUR_DEANZA_ID` and `Your Full Name` comments). The **Get ID** button on the main page then returns your ID and name.

## Quick test URLs

- Home: http://localhost:8080/
- Get ID: http://localhost:8080/api/getid
- Airline by IATA: http://localhost:8080/api/airline/AA
- Report airlines (2.2a): http://localhost:8080/api/report/airlines
- Report airports (2.2b): http://localhost:8080/api/report/airports
- Airlines at SFO: http://localhost:8080/api/airlines_at/SFO
- Top 3 cities for AA: http://localhost:8080/api/top_cities/AA/3
- Distance SFO–ORD: http://localhost:8080/api/distance/SFO/ORD
- Search airlines: http://localhost:8080/api/airlines?search=American
- Search airports: http://localhost:8080/api/airports?search=SFO

## Project layout

```
CapstoneProject/
  CMakeLists.txt
  vcpkg.json
  src/
    main.cpp          # Crow app, CSV loading, API and HTML routes
  data/
    airports.dat      # from OpenFlights
    airlines.dat
    routes.dat
  download_data.ps1   # PowerShell download script
  download_data.bat   # Batch download script (curl)
  README.md
```

## Deployment (optional)

For remote access (e.g. for grading), you can:

- Use **ngrok** or similar to expose port 8080: `ngrok http 8080`.
- Or deploy to a cloud host (e.g. fly.io) by building the C++ app for Linux and running it there (see HonorsInfo PDF for hints).

## License / data

Application code: as required by your course.  
OpenFlights data: [Open Database License](https://opendatacommons.org/licenses/odbl/1-0/).
