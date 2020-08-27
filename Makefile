CC 	= gcc
MKDIR	= mkdir
RM	= rm -fr
TARGET 	= lfq-test
OBJDIR 	= .objs
CSRC 	= $(shell ls *.c)
OBJS 	= $(patsubst %.c,$(OBJDIR)/%.o, $(CSRC))
LDFLAGS	= -lpthread
ARCH	= sandybridge
CFLAGS	= -DARCH=\"${ARCH}\" -march=${ARCH} \
	  -mcx16 -ggdb -O3 -Wall -funroll-loops -fomit-frame-pointer

all: $(OBJDIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(OBJDIR)/%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS)
	$(RM) $(TARGET)
	$(RM) $(OBJDIR)

