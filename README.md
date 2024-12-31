# obos-strap
Yet another build tool for operating systems.
## Building
### Prerequisites
- A C compiler
- CMake
- cJSON installed somewhere where CMake can discover it.
### Building
1. Clone the repo and enter it's directory:
```sh
git clone https://github.com/OBOS-dev/obos-strap
cd obos-strap
```
2. Run CMake
```sh
cmake -Bbuild -DCMAKE_BUILD_TYPE=Release .
cmake --build build
```
3. The binary should be somewhere under build/
