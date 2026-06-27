# RTSL Intermediate Representation

RTSL does not currently expose a standalone, user-authored SSA IR. The compiler
lowers source into internal artifact data that is serialized into the binary
`rtslo`, `rtslm`, `rtsll`, and `rtslp` formats.

The important public layers today are:

- source language syntax
- semantic analysis
- artifact serialization
- reflection metadata
- backend GLSL emission

The current artifact format stores:

- strings
- types
- structs
- functions
- function debug data
- uniforms
- stage interfaces
- entry metadata
- raw bytecode / backend payload

Textual RTIR assembly and disassembly exist primarily as a debugging and test
representation of the serialized artifact. They are not a separate design
target and should be kept in sync with the current artifact schema.

## Current Status

The old SSA-like RTIR design in this document is obsolete. If the compiler
grows a dedicated IR in the future, this document should be rewritten from the
actual data model rather than inferred from older plans.
