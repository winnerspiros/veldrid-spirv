# Veldrid.SPIRV

[![CI](https://github.com/winnerspiros/veldrid-spirv/actions/workflows/build.yml/badge.svg)](https://github.com/winnerspiros/veldrid-spirv/actions/workflows/build.yml)

Veldrid.SPIRV is an extension library for [Veldrid](https://github.com/veldrid/veldrid) that provides support for loading SPIR-V bytecode for all Veldrid backends.

This is a modernized fork based on [ppy/veldrid-spirv](https://github.com/ppy/veldrid-spirv) (itself a fork of the original [mellinoe/veldrid-spirv](https://github.com/mellinoe/veldrid-spirv)).

## Changes from ppy/veldrid-spirv

### .NET & C# Modernization

- Upgraded all projects from .NET 5/8 to **.NET 10** (`net10.0`)
- Upgraded to latest C# language version with modern features:
  - File-scoped namespaces
  - Collection expressions (`[]` syntax)
  - Switch expressions
  - Primary constructors
  - Source-generated regex (`[GeneratedRegex]`)
  - Nullable reference types enabled across all projects
  - Implicit usings enabled
- Updated **Nerdbank.GitVersioning** 3.4.255 → 3.9.50
- Updated **xUnit** to xunit.v3 3.2.2 (latest)
- Updated **Microsoft.NET.Test.Sdk** to 18.4.0
- Updated **McMaster.Extensions.CommandLineUtils** 4.0 → 5.1.0
- Replaced deprecated `PackageLicenseUrl` with `PackageLicenseExpression`
- Removed deprecated `DotNetCliToolReference` and unnecessary `System.Runtime.CompilerServices.Unsafe` package
- Removed deprecated test runner `Program.cs` (xunit.v3 uses `dotnet test` directly)

### Bug Fixes (from upstream [veldrid/veldrid-spirv](https://github.com/veldrid/veldrid-spirv) issues/PRs)

- **[Issue #32](https://github.com/veldrid/veldrid-spirv/issues/32)**: Debug flag is now correctly propagated to cross-compiled shader descriptions in `ResourceFactoryExtensions`
- **[PR #34](https://github.com/veldrid/veldrid-spirv/pull/34)**: Added `#include <cstdint>` for GCC 10+ build compatibility in the native C++ library
- **[PR #21](https://github.com/veldrid/veldrid-spirv/pull/21)**: `SpirvCompilationException` now includes `LineNumber` and `ShaderStage` in its `Data` dictionary for better error diagnostics
- **CompileCompute interop bug**: Fixed incorrect use of `SpecializationConstant` (managed struct) instead of `NativeSpecializationConstant` (interop struct) when passing specialization constants to native code — this could cause memory corruption or crashes
- **Null safety improvements**: Added null checks, removed unsafe null-forgiving operators, improved error handling in `VariantCompiler`
- **Code quality**: Extracted duplicate `JsonSerializer` setup, fixed confusing flag logic, removed dead code

### Android 16KB Page Size Support

- Added `-Wl,-z,max-page-size=16384` linker flag in `CMakeLists.txt` for Android builds, required for **Android 16 (API level 36+)** which mandates 16KB page-aligned shared libraries. See the [Android developer documentation](https://developer.android.com/guide/practices/page-sizes) for details.

### CI/CD Workflow

- .NET SDK updated from 6.0 to **10.0**
- Test execution modernized: `dotnet run -p` → `dotnet test`
- Added **linux-arm64** to the test matrix
- Added `workflow_dispatch` with `publish_nuget` boolean input for **one-click NuGet publishing** (just click "Run workflow" in the Actions tab and check "Publish NuGet package")
- Added `--skip-duplicate` to NuGet push to avoid failures on re-runs

## Usage

The easiest way to use Veldrid.SPIRV is through [the extension methods it provides for the ResourceFactory type](src/Veldrid.SPIRV/ResourceFactoryExtensions.cs).

```csharp
byte[] vertexShaderSpirvBytes = File.ReadAllBytes("myshader.vert.spv");
byte[] fragmentShaderSpirvBytes = File.ReadAllBytes("myshader.frag.spv");
Shader[] shaders = factory.CreateFromSpirv(
    new ShaderDescription(ShaderStages.Vertex, vertexShaderSpirvBytes, "main"),
    new ShaderDescription(ShaderStages.Fragment, fragmentShaderSpirvBytes, "main"));
// Use "shaders" array to construct a Pipeline
```

You can also directly load GLSL source code and do the same as above. Behind the scenes, Veldrid.SPIRV will compile the GLSL to SPIR-V and then perform the cross-compile to the target language.

## Specialization Constants

Although HLSL and OpenGL-style GLSL do not support SPIR-V Specialization Constants, you can use Veldrid.SPIRV to "specialize" the shader before the target source code is actually emitted. Set `CrossCompileOptions.Specializations` with an array of `SpecializationConstant` values to accomplish this.

## libveldrid-spirv

Veldrid.SPIRV is implemented primarily as a native library, interfacing with [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) and [shaderc](https://github.com/google/shaderc). There are build scripts in the root of the repository which can be used to automatically build the native library for your platform.

Native build requirements:

* CMake
* Python

Pre-built binaries are bundled in the NuGet package for the following platforms:

* Windows x64, x86, ARM64
* macOS universal (x64 + ARM64)
* Linux x64, ARM64
* iOS (device + simulator via XCFramework)
* Android arm64-v8a, armeabi-v7a, x86, x86_64

## Creating a NuGet Package

### One-click from GitHub Actions

1. Go to the **Actions** tab → **CI** workflow
2. Click **Run workflow**
3. Optionally check **Publish NuGet package to nuget.org** to push directly (requires `NUGET_API_KEY` secret)
4. Click **Run workflow**

The workflow builds native libraries for all platforms, runs tests, packs the NuGet package, and optionally publishes it.

### Locally

```bash
# Build native libraries for your platform
./build-native.sh release linux-x64  # or osx, etc.

# Pack NuGet
dotnet pack src/Veldrid.SPIRV -c Release
```

### From tag

Push a git tag (e.g., `v1.0.16`) to automatically trigger a build and NuGet publish.
