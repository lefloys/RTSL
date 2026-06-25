# Rutile Shading Language (RTSL)

Rutile Shading Language is a shader language and compiler for the Rutile graphics API.

The core idea is simple: a shader should live in one source file, with its resources, stage 
payloads, helper types, and entry points kept together. RTSL is designed to make that shader 
contract explicit and easy to read.

## What RTSL Is For

RTSL focuses on the parts of shader code that tend to get scattered across multiple files or 
implicit backend conventions:

- resource binding scopes via `uniform`
- inter-stage data via `varying`
- explicit stage entry points via `entry fn`
- familiar C-style syntax for structs, functions, control flow, and lexical scope


## Examples

The `workspace` directory contains sample shader files that show the intended style:

- `default.rtsl`
- `graphics.rtsl`
- `compute.rtsl`
- `advanced.rtsl`

These samples demonstrate:

- graphics pipeline stages
- compute shader structure
- uniform resource access
- varying payloads
- proposed advanced stage families such as tessellation, mesh, and ray tracing

## Current Status

RTSL is still a design and tooling project.

The repository describes the intended source language and includes editor
tooling scaffolding, but it does not yet define a final ABI for entry points,
binding numbers, or backend lowering rules.

## Getting Started

If you want to explore the language design, start with:

1. [`language.md`](./language.md)
2. [`groups.rtsl`](./groups.rtsl)
3. The files in [`workspace/`](./workspace)

If you want to work on editor support, open the Visual Studio extension under
[`tools/vs-rtsl-ext/`](./tools/vs-rtsl-ext).

