del apps\lightstrip-remote\lightstrip-remote.d
del apps\lightstrip-remote\lightstrip-remote.rel
del apps\lightstrip-remote\lightstrip-remote.wxl
del apps\lightstrip-remote\lightstrip-remote.hex
del apps\lightstrip-remote\lightstrip-remote
make
call wixelcmd write apps\lightstrip-remote\lightstrip-remote.wxl stripLength=1 radio_channel=10
