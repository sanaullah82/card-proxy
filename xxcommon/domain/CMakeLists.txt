
add_custom_command (
    OUTPUT DataToken.cpp DataKey.cpp Config.cpp VaultUser.cpp SecureVault.cpp
    COMMAND ${YBORM_ROOT}/bin/yborm_gen --domain ${PROJECT_SOURCE_DIR}/xxcommon/card_proxy_schema.xml
        ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${PROJECT_SOURCE_DIR}/xxcommon/card_proxy_schema.xml
)

add_custom_target (generate_domain DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/DataToken.cpp)

