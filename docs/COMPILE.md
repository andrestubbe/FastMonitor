# Building FastMonitor 🛠️

Complete build guide for compiling the native C++17 virtual display bridge and packaging the Java JAR.

---

## Prerequisites

*   **Windows 10 or 11 (64-bit)**
*   **JDK 17+** ([Eclipse Adoptium](https://adoptium.net/) or [Oracle JDK](https://www.oracle.com/java/technologies/downloads/))
*   **Visual Studio 2022 or 2026** (Community, Professional, or Enterprise) with "Desktop development with C++" workload
*   **Windows 10/11 SDK** (installed with Visual Studio)
*   **Maven 3.9+**

---

## Automated One-Click Build

FastMonitor includes an intelligent compilation script with automatic Visual Studio and JDK discovery:

```cmd
# In the FastMonitor repository root:
compile.bat
```

What `compile.bat` does automatically:
1. Queries `vswhere.exe` to locate the latest installed Visual Studio (VS 2026 or VS 2022).
2. Initializes the 64-bit developer environment (`vcvars64.bat`).
3. Auto-detects `JAVA_HOME` if not already set.
4. Compiles `native/fastmonitor.cpp` with `/O2` optimization and links against the Windows display, PnP, SetupAPI, and device-installation libraries.
5. Deploys `fastmonitor.dll` directly to:
   - `native/fastmonitor.dll`
   - `src/main/resources/native/fastmonitor.dll` (for inclusion inside JARs)
   - `%USERPROFILE%\.fastcore\native\fastmonitor\fastmonitor.dll` (for runtime lookup by FastCore)
6. The JAR does not bundle driver packages. If the MttVDD device is missing, FastMonitor downloads fixed MikeTheTech VDD and NefCon release assets from GitHub, verifies their SHA-256 hashes, then requests normal Windows administrator approval for installation. Driver setup requires an internet connection; the installed driver package remains on the system after the demo removes its temporary device node.

---

## Maven Java Packaging

Once the native DLL is compiled, build and install the module to your local Maven repository:

```bash
# Build and install to ~/.m2/repository
mvn clean install -DskipTests
```

---

## Manual C++ Compilation

If you prefer building manually from a Visual Studio Developer Command Prompt:

```cmd
cd native

cl /LD /EHsc /O2 /W3 /nologo ^
   /I"%JAVA_HOME%\include" ^
   /I"%JAVA_HOME%\include\win32" ^
   fastmonitor.cpp ^
   /link ^
   user32.lib ^
   gdi32.lib ^
   setupapi.lib ^
   cfgmgr32.lib ^
   newdev.lib ^
   shell32.lib ^
   /OUT:fastmonitor.dll ^
   /MACHINE:X64
```
