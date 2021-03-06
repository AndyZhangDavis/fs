#!/bin/bash

set -o pipefail
#set -xv # debug

# Absolute path of this file
CWD=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

#
# Logging helpers
#
log() {
    echo -e "${*}"
}

info() {
    log "Info: ${*}"
}
warning() {
    log "Warning: ${*}"
}
error() {
    log "Error: ${*}"
}
die() {
    error "${*}"
    exit 1
}

#
# Scoring helpers
#
TOTAL=0
ANSWERS=()

add_answer() {
	ANSWERS+=("${1},")
}

inc_total() {
	let "TOTAL++"
}

# Returns a specific line in a multi-line string
select_line() {
	# 1: string
	# 2: line to select
	echo $(echo "${1}" | sed "${2}q;d")
}

fail() {
	# 1: got
	# 2: expected
    log "Fail: got '${1}' but expected '${2}'"
}

pass() {
	# got
    log "Pass: ${1}"
}

compare_output_lines() {
	# 1: output
	# 2: expected
	# 3: point step
	declare -a output_lines=("${!1}")
	declare -a expect_lines=("${!2}")
	local pts_step="${3}"

	for i in ${!output_lines[*]}; do
		if [[ "${output_lines[${i}]}" == "${expect_lines[${i}]}" ]]; then
			pass "${output_lines[${i}]}"
			sub=$(bc<<<"${sub}+${pts_step}")
		else
			fail "${output_lines[${i}]}" "${expect_lines[${i}]}" ]]
		fi
	done
}

#
# Generic function for running FS tests
#
run_test() {
    # These are global variables after the test has run so clear them out now
	unset STDOUT STDERR RET

    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    RET="${?}"
    STDOUT=$(cat "${outfile}")
    STDERR=$(cat "${errfile}")

    # Deal with the possible timeout errors
    [[ ${RET} -eq 127 ]] && warning "Something is wrong (the executable probably doesn't exists)"
    [[ ${RET} -eq 124 ]] && warning "Command timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Generic function for capturing output of non-test programs
#
run_tool() {
    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    local ret="${?}"
    local stdout=$(cat "${outfile}")
    local stderr=$(cat "${errfile}")

    # Log the output
    [[ ! -z ${stdout} ]] && info "${stdout}"
    [[ ! -z ${stderr} ]] && info "${stderr}"

    # Deal with the possible timeout errors
    [[ ${ret} -eq 127 ]] && warning "Tool execution failed..."
    [[ ${ret} -eq 124 ]] && warning "Tool execution timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Phase 1
#

# Info on empty disk
run_fs_info() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 100
    run_test ./test_fs.x info test.fs
    rm -f test.fs

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "1")")
    line_array+=("$(select_line "${STDOUT}" "2")")
    line_array+=("$(select_line "${STDOUT}" "3")")
    line_array+=("$(select_line "${STDOUT}" "4")")
    line_array+=("$(select_line "${STDOUT}" "5")")
    line_array+=("$(select_line "${STDOUT}" "6")")
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")
    local corr_array=()
    corr_array+=("FS Info:")
    corr_array+=("total_blk_count=103")
    corr_array+=("fat_blk_count=1")
    corr_array+=("rdir_blk=2")
    corr_array+=("data_blk=3")
    corr_array+=("data_blk_count=100")
    corr_array+=("fat_free_ratio=99/100")
    corr_array+=("rdir_free_ratio=128/128")

    sub=0
    compare_output_lines line_array[@] corr_array[@] "0.125"

    inc_total
    add_answer "${sub}"
}

# Add by fs_ref.x 
# Info with files, size : 0, 2048, 4096, 4097, 1M 
run_fs_info_full() {
    log "\n--- Running ${FUNCNAME} ---"

    # run_tool ./fs_make.x test.fs 100  # not enough
	run_tool ./fs_make.x test.fs 300
    run_tool dd if=/dev/urandom of=test-file-0 bs=2048 count=0
	run_tool dd if=/dev/urandom of=test-file-1 bs=2048 count=1 
	run_tool dd if=/dev/urandom of=test-file-2 bs=2048 count=2 # 4096
    run_tool dd if=/dev/urandom of=test-file-3 bs=4097 count=1 # 4097
	run_tool dd if=/dev/urandom of=test-file-4 bs=1M count=1 # 1024K, 256 block
    run_tool ./fs_ref.x add test.fs test-file-0
	run_tool ./fs_ref.x add test.fs test-file-1
	run_tool ./fs_ref.x add test.fs test-file-2
    run_tool ./fs_ref.x add test.fs test-file-3
	run_tool ./fs_ref.x add test.fs test-file-4

	run_test ./test_fs.x info test.fs
    rm -f test-file-* test.fs
	# rm -f test-file-1 test-file-2 test-file-3 test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "7")")
	line_array+=("$(select_line "${STDOUT}" "8")")
	local corr_array=()
	corr_array+=("fat_free_ratio=39/300") # 0 + 1 + 1 + 2 + 256 + 1(0)
	corr_array+=("rdir_free_ratio=123/128")

	sub=0
	compare_output_lines line_array[@] corr_array[@] "0.5"

	inc_total
	add_answer "${sub}"
}

#
# Phase 2
# add with test_fs.x, ls with fs_ref.x
run_fs_simple_create() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool dd if=/dev/zero of=test-file-c1 bs=10 count=1
	run_tool timeout 2 ./test_fs.x add test.fs test-file-c1
	run_test ./fs_ref.x ls test.fs
	rm -f test.fs test-file-c1

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "2")")
	local corr_array=()
	corr_array+=("file: test-file-c1, size: 10, data_blk: 1")

	sub=0
	compare_output_lines line_array[@] corr_array[@] "1"
	inc_total
	add_answer "${sub}"


}


# Phase 2+
# add large file and  info, why only able to use ./fs_ref.x ls test.fs
run_fs_xM_create() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 300
    run_tool dd if=/dev/zero of=test-file-1M bs=1M count=1
    run_tool dd if=/dev/zero of=test-file-2M bs=2M count=1
    run_tool timeout 2 ./test_fs.x add test.fs test-file-1M
    # run_tool timeout 2 ./test_fs.x add test.fs test-file-2M

    # add file-1M, test ls
    

    local line_array=()
    local corr_array=()

    # run_test ./fs_ref.x ls test.fs
    run_test ./test_fs.x ls test.fs # why this doesn't work
    line_array+=("$(select_line "${STDOUT}" "2")")
    corr_array+=("file: test-file-1M, size: 1048576, data_blk: 1")

    sub=0
    compare_output_lines line_array[@] corr_array[@] "1"
    inc_total
    add_answer "${sub}"


    # test info
    line_array=()
    corr_array=()
    run_test ./test_fs.x info test.fs
    # run_test ./fs_ref.x info test.fs

    line_array+=("$(select_line "${STDOUT}" "1")")
    line_array+=("$(select_line "${STDOUT}" "2")")
    line_array+=("$(select_line "${STDOUT}" "3")")
    line_array+=("$(select_line "${STDOUT}" "4")")
    line_array+=("$(select_line "${STDOUT}" "5")")
    line_array+=("$(select_line "${STDOUT}" "6")")
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")

    corr_array+=("FS Info:")
    corr_array+=("total_blk_count=303")
    corr_array+=("fat_blk_count=1")
    corr_array+=("rdir_blk=2")
    corr_array+=("data_blk=3")
    corr_array+=("data_blk_count=300")
    corr_array+=("fat_free_ratio=43/300")
    corr_array+=("rdir_free_ratio=127/128")

    sub=0
    compare_output_lines line_array[@] corr_array[@] "0.125"
    inc_total
    add_answer "${sub}"

    # add file-2M, test ls 
    line_array=()
    corr_array=()
    run_tool timeout 2 ./test_fs.x rm test.fs test-file-1M
    run_tool timeout 2 ./test_fs.x add test.fs test-file-2M
    run_test ./test_fs.x ls test.fs
    # run_test ./fs_ref.x ls test.fs

    line_array+=("$(select_line "${STDOUT}" "2")")
    corr_array+=("file: test-file-2M, size: 1224704, data_blk: 1") # Wrote file 'test-file-5' (1224704/2097152 bytes)

    sub=0
    compare_output_lines line_array[@] corr_array[@] "1"
    inc_total
    add_answer "${sub}"

    # test info
    line_array=()
    corr_array=()
    run_test ./test_fs.x info test.fs
    # run_test ./fs_ref.x info test.fs

    line_array+=("$(select_line "${STDOUT}" "1")")
    line_array+=("$(select_line "${STDOUT}" "2")")
    line_array+=("$(select_line "${STDOUT}" "3")")
    line_array+=("$(select_line "${STDOUT}" "4")")
    line_array+=("$(select_line "${STDOUT}" "5")")
    line_array+=("$(select_line "${STDOUT}" "6")")
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")

    corr_array+=("FS Info:")
    corr_array+=("total_blk_count=303")
    corr_array+=("fat_blk_count=1")
    corr_array+=("rdir_blk=2")
    corr_array+=("data_blk=3")
    corr_array+=("data_blk_count=300")
    corr_array+=("fat_free_ratio=0/300")
    corr_array+=("rdir_free_ratio=127/128")

    sub=0
    compare_output_lines line_array[@] corr_array[@] "0.125"
    inc_total
    add_answer "${sub}"

    rm -f test.fs test-file-1M test-file-2M
}

# add two with test_fs.x, ls with fs_ref.x, within boundary
run_fs_create_multiple() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool dd if=/dev/zero of=test-file-1 bs=10 count=1
	run_tool dd if=/dev/zero of=test-file-2 bs=10 count=1
	run_tool timeout 2 ./test_fs.x add test.fs test-file-1
	run_tool timeout 2 ./test_fs.x add test.fs test-file-2

	run_test ./fs_ref.x ls test.fs

	rm -f test.fs test-file-1 test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "2")")
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("file: test-file-1, size: 10, data_blk: 1")
	corr_array+=("file: test-file-2, size: 10, data_blk: 2")

	sub=0
	compare_output_lines line_array[@] corr_array[@] "0.5"
	inc_total
	add_answer "${sub}"
}

#
# Run tests
#
run_tests() {
	# Phase 1
	run_fs_info
	run_fs_info_full
	# Phase 2
	run_fs_simple_create
    run_fs_xM_create # yuan: add large file
	run_fs_create_multiple # yuan: add two with test_fs.x, ls with fs_ref.x, within boundary
}

make_fs() {
    # Compile
    make > /dev/null 2>&1 ||
        die "Compilation failed"

    local execs=("test_fs.x" "fs_make.x" "fs_ref.x")

    # Make sure executables were properly created
    local x
    for x in "${execs[@]}"; do
        if [[ ! -x "${x}" ]]; then
            die "Can't find executable ${x}"
        fi
    done
}

cd "${CWD}"
make_fs
run_tests
