rm -rf .pio/build/
rm sdkconfig.seeed_xiao_esp32c6   
io run --target clean
pio run --target upload
