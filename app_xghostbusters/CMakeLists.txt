cmake_minimum_required(VERSION 3.21)
set(XMOS_SANDBOX_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "Directory containing the sibling XMOS dependency repositories")
include("${XMOS_SANDBOX_DIR}/xcommon_cmake/xcommon.cmake")
project(xghostbusters)
set(APP_HW_TARGET custom_xu316.xn)
set(APP_DEPENDENT_MODULES lib_mic_array lib_xcore_math lib_spi lib_sw_pll lib_voice)
set(APP_INCLUDES src "${XMOS_SANDBOX_DIR}/lib_spi/lib_spi/src")
set(APP_XSCOPE_SRCS "")
set(APP_COMPILER_FLAGS -O3 -g -report -mcmodel=large
    -DMIC_ARRAY_CONFIG_MIC_COUNT=2 -DMIC_ARRAY_CONFIG_MIC_IN_COUNT=2
    -DMIC_ARRAY_CONFIG_USE_PDM_ISR=0 -DMIC_ARRAY_CONFIG_SAMPLES_PER_FRAME=32)
XMOS_REGISTER_APP()
