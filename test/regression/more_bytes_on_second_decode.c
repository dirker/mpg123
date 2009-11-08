/*
I'm seeing something unexpected with seeks, and I'm not sure if I'm
doing something wrong. The situation is as follows: I open a gaplessly
encoded MP3 with mpg123_open, call mpg123_read until I get
MPG123_DONE, and add up the total number of decoded bytes. This number
matches the length of the original PCM audio. Then I seek to offset 0
and repeat the process. This time MPG123_DONE will be returned in a
different, later place (i.e., the new total number of bytes is
larger). Further repetitions always give the new, larger number.
However, I would expect that MPG123_DONE should always be returned
after the same number of bytes.

Here is an example of what I mean:

  for (iteration = 1; iteration <= maxiter; iteration++) {
    printf("Decoding %s (time %d of %d)...\n", filename, iteration, maxiter);
    total = 0;
    do {
      mpg123_err = mpg123_read(mh, buf, sizeof buf, &written);
      total += written;
    } while (mpg123_err != MPG123_DONE);
    printf("Got %u bytes in all.\n", total);
    mpg123_seek(mh, 0, SEEK_SET);
  }

The output of this is:

Decoding yuka.mp3 (time 1 of 5)...
Got 12296384 bytes in all.    // correct value
Decoding yuka.mp3 (time 2 of 5)...
Got 12298940 bytes in all.    // incorrect
Decoding yuka.mp3 (time 3 of 5)...
Got 12298940 bytes in all.
(...)

If I compare the PCM output for the runs, they all compare identical
except for extra data at the end for runs 2 and up.

So, what is wrong here?
*/

#include <mpg123.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	mpg123_handle *mh;
	int i;
	off_t counts[2];
	char buf[16384];
	mpg123_init();
	mh = mpg123_new(NULL, NULL);
	int ret = -1;
printf("open nothing: %i\n",mpg123_open(mh, NULL));

	if(argc < 2 || mpg123_open(mh, argv[1]) != MPG123_OK)
	{
		fprintf(stderr, "Hey, give me a (valid) mpeg file to work on!\n");
		goto end;
	}

	for(i = 0; i < 2; i++)
	{
		int mpg123_err;
		counts[i] = 0;
		do
		{
			size_t written;
			mpg123_err = mpg123_read(mh, buf, sizeof buf, &written);
			counts[i] += written;
			if(mpg123_err == MPG123_ERR)
			{
				fprintf(stderr, "Read error!\n");
				goto end;
			}
		} while(mpg123_err != MPG123_DONE);
		mpg123_seek(mh, 0, SEEK_SET);
	}

	fprintf(stderr, "First:  %"PRIiMAX"\n", (intmax_t) counts[0]);
	fprintf(stderr, "Second: %"PRIiMAX"\n", (intmax_t) counts[1]);
	if(counts[0] == counts[1]) ret = 0;

end:
	mpg123_delete(mh);
	mpg123_exit();
	printf("%s\n", ret == 0 ? "PASS" : "FAIL");
	return ret;
}
