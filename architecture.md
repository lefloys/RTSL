# RTSL Compiler Architecture

This document captures the current compiler/toolchain direction. It is not the
language specification. It describes how source files become Rutile shader
artifacts and how those artifacts are intended to be consumed by the runtime and
backends.

## Goals

- RTSL source is compiled through a C++ frontend with a stable C ABI.
- Runtime users can call the compiler directly through the C interface.
- Tool users can call a standalone executable in the style of `gcc` or `cl`.
- Authored code uses source files only. There are no user-written header files.
- Imported files are consumed through compiler-emitted interface artifacts, not
  by reparsing source text.
- The final linked program is backend-neutral. Rutile backends lower it to HLSL,
  SPIR-V, MSL, or another backend representation.

## Artifacts

RTSL source files produce Rutile shader artifacts. These artifacts are no longer
"RTSL source"; they are compiler outputs.

### `.rtso` - Rutile Shader Object

An `.rtso` file is the compiled object for one source file.

It contains:

- compiled RTIR function bodies
- local/private symbols
- exported symbol definitions
- unresolved symbol references
- call references
- type and constant tables
- optional debug/source metadata

The linker consumes `.rtso` files.

### `.rtsm` - Rutile Shader Module

An `.rtsm` file is the compiled module output for one source file.

It is emitted when the source file exports at least one symbol. Compiling a
source file always produces an `.rtso`; if that source file exports symbols, it
also produces an `.rtsm`.

It contains all exported symbols of that source file:

- exported function signatures
- exported type declarations
- exported constants
- exported resource declarations, if the language allows exporting them
- the original import path identity
- interface/version hashes for stale-artifact validation
- dependency/interface metadata

It does not contain private symbols.

The compiler consumes `.rtsm` files while compiling imports.

### `.rtsp` - Rutile Shader Program

An `.rtsp` file is the linked shader program.

It contains:

- all required functions and symbols resolved
- final linked RTIR
- entry point metadata
- resource and stage-interface metadata
- type and constant tables
- optional debug/source metadata

Rutile backends consume `.rtsp` files and lower them to backend-specific shader
formats.

## Source Files And Exports

Every `.rtsl` source file is an implementation file. It may choose to export any
symbol.

Example:

```rtsl
export fn saturate(f32 x) -> f32 {
    return clamp(x, 0.0, 1.0);
}

fn helper(f32 x) -> f32 {
    return x * 2.0;
}
```

Compiling this source emits:

```text
math.rtso
math.rtsm
```

If a file exports nothing, it only emits an `.rtso`.

Forward declarations are still allowed inside a source file for local ordering
convenience. They are not a header or interface mechanism.

## Imports

Imports are file-oriented.

```rtsl
import <math.rtsl>;
import <lighting/brdf.rtsl>;
```

The import names the source-like path, but the compiler does not parse that
source file while compiling the importer. Instead, it resolves the path to the
compiled interface artifact produced by that source file.

Direct mapping:

```text
math.rtsl -> math.rtsm
lighting/brdf.rtsl -> lighting/brdf.rtsm
```

When build outputs live elsewhere, include/module search paths preserve the same
relative shape:

```text
import <lighting/brdf.rtsl>
with -I build/rtsl
resolves to build/rtsl/lighting/brdf.rtsm
```

The module artifact is a compiler input only. It lets the compiler analyze
imports without reparsing imported source files.

## Symbol Model

Object files use tables, not pointers.

An unlinked `.rtso` contains:

- string table
- type table
- function table
- symbol table
- instruction stream

A locally defined function has both a function table entry and a symbol table
entry.

An imported function has a symbol table entry with no local function body.

In an `.rtso`, a call references a symbol id:

```text
call symbol_id
```

In a linked `.rtsp`, direct calls should be rewritten to function ids:

```text
call function_id
```

Function ids are indices into the linked program's function table. They are not
runtime function pointers.

Function identity is based on a canonical symbol key, not just the short name:

```text
qualified_name + parameter_types + return_type
```

This keeps overloaded functions linkable and unambiguous.

## Linking

The linker is shader-specific. It does not perform CPU-style address relocation
or produce hardware machine code.

The linker resolves:

- imported symbols
- exported definitions
- cross-module function calls
- type identity and ABI compatibility
- final resource/stage metadata needed by the backend

Inputs:

```text
main.rtso
math.rtso
lighting/brdf.rtso
```

Output:

```text
program.rtsp
```

The compiler uses and produces `.rtsm` files. The linker does not require
`.rtsm` files. It operates on `.rtso` inputs only and produces an `.rtsp`.

## RTIR

RTIR is the backend-neutral Rutile shader IR.

The canonical serialized form is binary and lives inside `.rtso`, `.rtsm`, and
`.rtsp` artifacts.

A textual RTIR form may exist for diagnostics, tests, snapshots, and manual
inspection:

```text
.rtir
```

The text form is a development/debugging aid. Production tools should consume the
binary artifacts.

The compiler path should be:

```text
RTSL source -> binary artifact -> optional text RTIR dump
```

It should not be:

```text
RTSL source -> text RTIR -> parse text RTIR -> binary artifact
```

## Calls And Primitives

RTIR supports direct static calls. RTSL does not need function pointers for the
initial design.

A call instruction names a symbol before linking and a function id after linking.

Standard library functions are ordinary functions. They can call reserved
primitive functions when they need behavior that cannot be expressed in RTSL.

Example:

```rtsl
namespace std {
    export fn sample(Sampler2D tex, vec2 uv) -> vec4 {
        return rt::__primitive::texture_sample_implicit_lod(tex, uv);
    }
}
```

In this model:

- `sample` is a normal standard library function.
- `rt::__primitive::texture_sample_implicit_lod` is the primitive.
- Backends only need special lowering for primitive calls.

The primitive namespace is reserved by the language in the same way keywords are
reserved. User code cannot define, shadow, import-alias over, or export names in
that namespace.

## Standard Library

The standard library should be written in RTSL where possible.

It maps user-facing functions such as `sample`, `sample_lod`, `normalize`,
`saturate`, and `mix` onto ordinary RTSL code or reserved primitives.

Only operations that cannot be implemented in RTSL should be primitives, such as:

- texture sampling
- buffer/image loads and stores, if not expressible otherwise
- barriers
- derivatives
- discard
- subgroup operations
- ray tracing operations

This keeps the backend contract small while preserving a normal function-call
model for user code.

## CLI Shape

The command-line tool should support both object/interface compilation and final
program linking.

Possible shape:

```powershell
rtslc -c math.rtsl
```

Emits:

```text
math.rtso
math.rtsm
```

For a source file with no exports, only `math.rtso` is required.

Final linking:

```powershell
rtslc main.rtso math.rtso lighting/brdf.rtso -o app.rtsp
```

Compilation with import search paths:

```powershell
rtslc -c main.rtsl -I build/rtsl
```

When `main.rtsl` imports `<lighting/brdf.rtsl>`, the compiler reads:

```text
build/rtsl/lighting/brdf.rtsm
```

## C ABI Shape

The C ABI should expose the same pipeline as the CLI:

- create/destroy context
- compile source to object/interface artifacts
- add interface search paths or in-memory interface blobs
- create/destroy linker
- add object blobs/modules to linker
- link program
- read output blobs

The runtime and command-line executable should share the same implementation.
