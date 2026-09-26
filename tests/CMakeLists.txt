set(CMAKE_AUTOMOC ON)

add_executable(protocol_tests
    protocol_tests.cpp
)

target_link_libraries(protocol_tests
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        qtlink_protocol
)

add_test(
    NAME protocol_tests
    COMMAND protocol_tests
)

add_executable(stage12_tests
    stage12_tests.cpp
    ../server/tcpserver.cpp
    ../client/connectionmanager.cpp
    ../client/filetransfermanager.cpp
)

target_include_directories(stage12_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/server
        ${CMAKE_SOURCE_DIR}/client
)

target_link_libraries(stage12_tests
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Network
        qtlink_protocol
)

add_test(
    NAME stage12_tests
    COMMAND stage12_tests
)
