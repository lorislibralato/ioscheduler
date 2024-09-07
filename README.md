### Update
`git pull origin main && git submodule update --remote`

### Compile
`make build BUILD_DIR=$(pwd)/build && rm -f test.db && ./build/main`

### Run
`rm -f test.db && ./build/main test.db`