<div align="center"> <img src="assets/svg/Blue_petrichor.svg" alt="Logo" width="400"/> </div>

# Petrichor

Petrichor is an opinionated game-agnostic C++ modding framework written for:
* Embeddability
* Flexibility
* Simplicity
* Safety
* Power
* Speed

The language we chose is [Luau, a fork of Lua 5.1](https://luau.org/). We chose this language for its sandboxing, native compilation, dynamic typing, and overall elegance when writing mods.

## Dependencies

* Luau (vendored)
* miniz (vendored)
* sol2 (vendored)

## Architecture

```
petrichor_run(IPetrichorHost& host, const char* format)
↓
Loader      ← Iterates through mods folder
↓
Compilation ← Compiles preludes, host modules, and mods with Luau
```

## Library Structure

Library structure (built [here](https://tree.nathanfriend.com/?s=(%27opt4s!(%27fancy!true~fullPath!false~trailingSlash!true~rootDot!false)~F(%27F%27PB3tests7K0test%20suites3cmake7K0cmake%20G%20%7Be.g.%20dependencies%7D3includeRaugment3KpB.h5public-facing%20API3srcRHs70EH%206GRJ.cpp%2Fh*0miniz%20J%20extrac9wrappe8Rloader.cppK5Top-level%20API%20implementat4sQtools%2F3%20bin2h.pyK*5helpe8fo8Eto%20C%2B6heade8conNO%20CMakeLists.txt*5configura96installa9script%27)~N!%271%27)*%205%E2%86%90QO*4ion5%2006%2B%207%2FK*8r%209t4%20BetrichorELuau%20Fsource!GmodulesHbackendJarchiveK**Nvers4O%5CnQ%203R3*%01RQONKJHGFEB98765430*))

```
Petrichor/
├── tests/              ← test suites
├── cmake/              ← cmake modules (e.g. dependencies)
├── include/
│   └── augment/
│       └── petrichor.h ← public-facing API
├── src/
│   ├── backends/       ← Luau backend + modules
│   ├── archive.cpp/h   ← miniz archive extraction wrapper 
│   └── loader.cpp      ← Top-level API implementations 
├── tools/
│   └── bin2h.py        ← helper for Luau to C++ header conversion
└── CMakeLists.txt      ← configuration + installation script
```

## Integration

Simply wire in your project information & implementations of `IPetrichorHost`, and call into the functions documented in `petrichor.h`. Clean documentation can be found on our [ReadTheDocs](https://petrichor.readthedocs.io/en/latest/) page.

