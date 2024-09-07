### Update
`git pull origin main && git submodule update --remote`

### Compile
`make build BUILD_DIR=$(pwd)/build`

### Run
`rm -f test.db && ./build/main test.db`