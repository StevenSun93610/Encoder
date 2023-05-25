# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
CFLAGS  = -g -pthread

# the build target executable:
TARGET = nyuenc

nyuenc: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) 

clean:
	$(RM) $(TARGET)