# How to Open and Run the Air Travel Database

Follow these steps in order.

---

## Step 0: Install a C++ compiler (required for build)

The error **“vcpkg was unable to detect the active compiler”** or **“No CMAKE_C_COMPILER could be found”** means Windows doesn’t have a C++ compiler that vcpkg/CMake can use.

**Install Visual Studio with C++:**

1. Download **Visual Studio 2022** (Community is free):  
   https://visualstudio.microsoft.com/downloads/
2. Run the installer and select the workload **“Desktop development with C++”**.
3. Install (this can take a while).
4. When you build (Step 3), **use one of these** so the compiler is on your PATH:
   - **Developer PowerShell for VS 2022**, or  
   - **x64 Native Tools Command Prompt for VS 2022**  
   (from Start menu: search for “Developer PowerShell” or “Native Tools Command Prompt”).

**Alternative – Build Tools only (no full IDE):**  
- Install **“Build Tools for Visual Studio 2022”** from the same download page, then select **“Desktop development with C++”**.

After installing, **close and reopen** your terminal, and use **Developer PowerShell** or **x64 Native Tools Command Prompt** when you run `cmake` in Step 3.

---

## Step 1: Get the data files

The app needs three CSV files in the `data` folder.

**Option A – PowerShell (recommended)**  
Open PowerShell, go to the project folder, then run:

```powershell
cd "c:\Users\HP\OneDrive\Рабочий стол\CapstoneProject"
.\download_data.ps1
```

**Option B – Manual download**  
1. Open: https://github.com/jpatokal/openflights/tree/master/data  
2. Download these three files (Right‑click → Save link as):
   - [airports.dat](https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat)
   - [airlines.dat](https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat)
   - [routes.dat](https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat)
3. Put all three inside the project’s `data` folder.

Check: the folder `CapstoneProject\data` should contain `airports.dat`, `airlines.dat`, and `routes.dat`.

---

## Step 2: Install vcpkg (only once)

If you don’t have vcpkg yet:

1. Open a terminal (PowerShell or Command Prompt).
2. Go to a folder where you keep tools, for example:
   ```bat
   cd C:\
   ```
3. Clone vcpkg and run the bootstrap script:
   ```bat
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```
4. Remember the path, e.g. `C:\vcpkg`. You’ll use it in the next step.

---

## Step 3: Build the project

**Easiest:** Double‑click **`build_and_run.bat`** in the project folder. It will build (using Visual Studio) and then start the server. Then open http://localhost:8080 in your browser (Step 5).

**Or use the command line:**

1. Open **Developer PowerShell for VS 2022** or **x64 Native Tools Command Prompt for VS 2022** (see Step 0). Do not use a normal Command Prompt/PowerShell if you get “No CMAKE_C_COMPILER could be found”.
2. Go to the project folder:
   ```bat
   cd "c:\Users\HP\OneDrive\Рабочий стол\CapstoneProject"
   ```
3. Create a build folder and run CMake (replace `C:\vcpkg` with your vcpkg path if different):
   ```bat
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
   ```
4. Compile:
   ```bat
   cmake --build . --config Release
   ```

When it finishes, the program is at:  
`build\Release\air_travel_app.exe` (or `build\air_travel_app.exe` on some setups).

---

## Step 4: Run the app

**Easiest:** Double‑click **`run_server.bat`** (use this after you’ve already built once with `build_and_run.bat`).

**Or run manually:**  
Make sure the **data** folder is next to the executable.  
- If you run from the **project root**, the app looks for `data` in the current directory, so run:
  ```bat
  cd "c:\Users\HP\OneDrive\Рабочий стол\CapstoneProject"
  .\build\Release\air_travel_app.exe
  ```
- Or run from inside `build` (CMake copies `data` into `build`):
  ```bat
  cd "c:\Users\HP\OneDrive\Рабочий стол\CapstoneProject\build"
  .\Release\air_travel_app.exe
  ```
You should see the server start (it runs in the terminal). Leave that window open.

---

## Step 5: Open it in your browser

1. Open **Chrome**, **Edge**, or **Firefox**.
2. In the address bar type:
   ```
   http://localhost:8080
   ```
3. Press Enter.

You should see the **Air Travel Database** home page. From there you can:

- Use **Airlines** / **Airports** to search.
- Use **Query** for quick links (e.g. airlines at SFO, distance SFO–ORD).
- Or open these directly:
  - http://localhost:8080/api/airlines_at/SFO  
  - http://localhost:8080/api/distance/SFO/ORD  

---

## Troubleshooting

| Problem | What to do |
|--------|------------|
| **“vcpkg was unable to detect the active compiler”** or **“No CMAKE_C_COMPILER could be found”** | Install Visual Studio 2022 with **“Desktop development with C++”** (Step 0). Then run CMake from **Developer PowerShell** or **x64 Native Tools Command Prompt**, not a normal terminal. |
| **“Check for working CXX compiler … broken”** or **“Cannot open source file … ??????? ????”** | Your project path contains **non‑English characters** (e.g. Cyrillic “Рабочий стол”). Move the whole **CapstoneProject** folder to a path with only English letters/numbers, e.g. **`C:\CapstoneProject`**, then build from there. |
| “Failed to load data” when starting the app | Ensure `data\airports.dat`, `airlines.dat`, and `routes.dat` exist. Run Step 1 again. |
| CMake can’t find Crow | Check that `-DCMAKE_TOOLCHAIN_FILE=...` points to `vcpkg\scripts\buildsystems\vcpkg.cmake`. |
| Nothing at http://localhost:8080 | Make sure `air_travel_app.exe` is still running (terminal window open) and you use port **8080**. |
| Port 8080 already in use | Close the other program using 8080, or we can change the app to use another port. |

When you’re done testing, close the terminal window where `air_travel_app.exe` is running to stop the server.
