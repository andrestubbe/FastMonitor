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
4. Compiles `native/fastmonitor.cpp` with `/O2` optimization and links against `user32.lib`, `gdi32.lib`, and `setupapi.lib`.
5. Deploys `fastmonitor.dll` directly to:
   - `native/fastmonitor.dll`
   - `src/main/resources/native/fastmonitor.dll` (for inclusion inside JARs)
   - `%USERPROFILE%\.fastcore\native\fastmonitor\fastmonitor.dll` (for runtime lookup by FastCore)

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
   /OUT:fastmonitor.dll ^
   /MACHINE:X64
```
