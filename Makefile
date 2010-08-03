
port_adaptor: wrapper.o
	${LINK.c} -o $@ wrapper.o

wrapper.o: src/unix_adaptor.c src/windows_adaptor.c src/wrapper.c
	${COMPILE.c} src/wrapper.c

clean:
	$(RM) wrapper.o port_adaptor port_adaptor.exe
