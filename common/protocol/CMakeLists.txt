add_library(qtlink_protocol STATIC
    protocol.h
    packet.h

    packetcodec.h
    packetcodec.cpp

    packetparser.h
    packetparser.cpp

    textmessagecodec.h
    textmessagecodec.cpp

    onlineclientlistcodec.h
    onlineclientlistcodec.cpp

    filetransfercontrolcodec.h
    filetransfercontrolcodec.cpp
    filechunkcodec.h filechunkcodec.cpp
)

target_include_directories(qtlink_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qtlink_protocol
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)