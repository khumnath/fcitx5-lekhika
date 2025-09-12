# SVG icon (scalable)
install(FILES lekhika.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
)

# PNG icon (fallback for legacy or HiDPI)
install(FILES lekhika.png
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps"
)
