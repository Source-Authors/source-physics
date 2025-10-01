# Source Physics (VPhysics)🛠️ 

Source Physics is Source's VPhysics default implementation which uses IVP/Havok engine.<br><br>
As there are no sources, it was partially restored from the assembly by https://github.com/nillerusr,
https://github.com/er2off, https://github.com/steadyfield, https://github.com/exstrim401,
https://github.com/HappyDOGE, https://github.com/RaphaelIT7, https://github.com/PiMoNFeeD and others.

## Features

VPhysics is mostly feature complete!

Below is a feature table of Volt vs VPhysics and the Bullet VPhysics project from [VPhysics Jolt](https://github.com/misyltoad/VPhysics-Jolt/blob/main/README.md).

| Feature       | VPhysics | Volt (VPhysics Jolt) | Bullet VPhysics |
|:--------------|:--------:|:-------------:|:---------------:|
| Constraints (except Pulleys)                                  | ✔️ | ✔️ | ✔️ |
| Pulleys                                                       | ✔️ | ✔️ | ❌ |
| Breakable constraints                                         | ✔️ | ❌ | ❌ |
| Motors (Motion Controllers)                                   | ✔️ | ✔️ | ✔️ |
| Motors (Constraint)                                           | ✔️ | ✔️ | ❌ |
| Ragdolls                                                      | ✔️ | ✔️ | ✔️ |
| Triggers                                                      | ✔️ | ✔️ | ❌ |
| Object touch callbacks                                        | ✔️ | ✔️ | ❌ |
| Prop damage/breaking                                          | ✔️ | ✔️ | ❌ |
| Fluid events                                                  | ✔️ | ✔️ | ❌ |
| Prop splashing effects                                        | ✔️ | ✔️ | ❌ |
| Wheeled Vehicles                                              | ✔️ | ✔️ | ✔️ |
| Raycast Vehicles (ie. Airboat)                                | ✔️ | ❌ | 〰️ (janky) |
| NPCs/Doors (Shadow Controllers)                               | ✔️ | ✔️ | ✔️ |
| Save/Restore Support                                          | ✔️ | ✔️ | ❌ |
| Portal Support                                                | ✔️ | ✔️ | ❌ |
| Game per-object collide callback support<br>eg. no-collide    | ✔️ | ✔️ | ❌ |
| Crash-resistant solver                                        | ❌ | ✔️ | (no data) |
| Supports thousands of objects without lag                     | ❌ | ✔️ | ❌ |
| Multithreaded                                                 | ❌ | ✔️ | ✔️ (partially) |
| Proper player controller                                      | ✔️ | ✔️ | ✔️ (partially) |

## Bugs

VPhysics is not without its flaws, however. See the [issue tracker](https://github.com/Source-Authors/source-physics/issues) for bugs that are known.

There are definitely going to be bugs that we don't know about or haven't encountered, or different quirks across engine branches.

## How to build

The VPhysics code is provided as-is, it is up to you to build it for your SDK, etc.

VPhysics should build fine against Source SDK 2013 and Alien Swarm SDK on a MSVC, Clang or GCC compiler with at least C++17 support.

*If you are building directly against the public SDK 2013 and Alien Swarm SDKs, you will need to do minor work to memoverride.cpp to make it compatible with the newer compilers and newer Windows SDKs.*

