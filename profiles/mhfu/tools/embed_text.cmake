# Turns a text file into string literals the host compiles in, one raw literal
# a line, which the compiler joins (MSVC limits a single literal to 16 KB):
#   cmake -DINPUT=<file> -DOUTPUT=<file.inc> -P embed_text.cmake
# The file is only rewritten when its content changes.
file(READ "${INPUT}" text)
if(text MATCHES "\\)embedded\"")
    message(FATAL_ERROR "${INPUT} contains the literals' closing delimiter")
endif()
string(REPLACE "\n" "\n)embedded\"\nR\"embedded(" text "${text}")
get_filename_component(name "${INPUT}" NAME)
set(content "// Generated from ${name} by profiles/mhfu/tools/embed_text.cmake.\nR\"embedded(${text})embedded\"\n")
set(previous "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()
