add_executable(seec-cc
               main.cpp
               cc1_main.cpp
               SeeCExecuteCompilerInvocation.cpp)

# Determine the LLVM libraries that we need
llvm_map_components_to_libnames(REQ_LLVM_LIBRARIES ${LLVM_TARGETS_TO_BUILD} analysis codegen core coroutines ipo instcombine instrumentation lto mc mcparser objcarcopts option passes scalaropts support transformutils vectorize asmparser bitreader bitwriter irreader linker profiledata coverage)

# Determine ICU libraries that we need
EXEC_PROGRAM(sh
 ARGS "${ICU_INSTALL}/bin/icu-config --noverify --prefix=${ICU_INSTALL} --ldflags-libsonly"
 OUTPUT_VARIABLE REQ_ICU_LIBRARIES
)
string(STRIP ${REQ_ICU_LIBRARIES} REQ_ICU_LIBRARIES)
string(REPLACE "-l" "" REQ_ICU_LIBRARIES ${REQ_ICU_LIBRARIES})
string(REPLACE " " ";" REQ_ICU_LIBRARIES ${REQ_ICU_LIBRARIES})

if (WIN32)
  set(CLANG_LIB_DEPS "${CLANG_LIB_DEPS};version.lib")
endif (WIN32)

target_link_libraries(seec-cc
 # SeeC libraries
 SeeCClang
 SeeCBreakConstantGEPs
 SeeCICU

 # Clang libraries
 clangBasic
 clangCodeGen
 clangDriver
 clangFrontend
 clangFrontendTool

 # LLVM libraries
 ${REQ_LLVM_LIBRARIES}

 # ICU libraries
 ${REQ_ICU_LIBRARIES}

 ${LLVM_LIB_DEPS}
 
 ${CLANG_LIB_DEPS}
)

INSTALL(TARGETS seec-cc
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/man/man1/seec-cc.1
        DESTINATION ${MAN_INSTALL_DIR}/man1)

