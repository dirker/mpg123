/****************************************************************************
 *              Copyright 2000 by Microware Systems Corporation             *
 *                           All Rights Reserved                            *
 *                         Reproduced Under License                         *
 *                                                                          *
 * This is a derivative work of the MP3 library from the mpg123 player,     *
 * specifically the 0.59r version. The home page for the player is          *
 * http://mpg123.org/ This software is under the GPL license. The GPL.TXT   *
 * file included with this software explains the license. Michael Hipp the  *
 * author of the mpg123 player may still be doing licenses other than GPL.  *
 *                                                                          *
 ****************************************************************************
 *			                                                                *  
 * Edition History:														    *
 *																		    *
 * #   Date     Comments                                                By  *
 * --- -------- -----------------------------------------------------  ---  *
 *   1 10/23/00 First working release                                  GbG  *
 *   2 10/24/00 Added STATIC define.                                   GbG  *
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>

#include "mpg123.h"
#include "mpglib.h"

/* Global mp .. it's a hack */
struct mpstr *gmp;


BOOL InitMP3(struct mpstr *mp) 
{
	static int init = 0;

	memset(mp,0,sizeof(struct mpstr));

	mp->framesize = 0;
	mp->fsizeold = -1;
	mp->bsize = 0;
	mp->head = mp->tail = NULL;
	mp->fr.single = -1;
	mp->bsnum = 0;
	mp->synth_bo = 1;

	if(!init) {
		init = 1;
		make_decode_tables(32767);
		init_layer3(SBLIMIT);
	}

	return !0;
}

void ExitMP3(struct mpstr *mp)
{
	struct buf *b,*bn;
	
	b = mp->tail;
	while(b) {
		free(b->pnt);
		bn = b->next;
		free(b);
		b = bn;
	}
}

STATIC struct buf *addbuf(struct mpstr *mp,char *inbuf,int size)
{
	struct buf *nbuf;

	nbuf = (struct buf *) malloc( sizeof(struct buf) );
	if(!nbuf) {
		fprintf(stderr,"Out of memory!\n");
		return NULL;
	}
	nbuf->pnt = (unsigned char *) malloc(size);
	if(!nbuf->pnt) {
		free(nbuf);
		return NULL;
	}
	nbuf->size = size;
	memcpy(nbuf->pnt,inbuf,size);
	nbuf->next = NULL;
	nbuf->prev = mp->head;
	nbuf->pos = 0;

	if(!mp->tail) {
		mp->tail = nbuf;
	}
	else {
	  mp->head->next = nbuf;
	}

	mp->head = nbuf;
	mp->bsize += size;

	return nbuf;
}

STATIC void remove_buf(struct mpstr *mp)
{
  struct buf *rbuf = mp->tail;
  
  mp->tail = rbuf->next;
  if(mp->tail)
    mp->tail->prev = NULL;
  else {
    mp->tail = mp->head = NULL;
  }
  
  free(rbuf->pnt);
  free(rbuf);

}

STATIC int read_buf_byte(struct mpstr *mp)
{
	unsigned int b;

	int pos;

	pos = mp->tail->pos;
	while(pos >= mp->tail->size) {
		remove_buf(mp);
		pos = mp->tail->pos;
		if(!mp->tail) {
			fprintf(stderr,"Fatal error!\n");
			exit(1);
		}
	}

	b = mp->tail->pnt[pos];
	mp->bsize--;
	mp->tail->pos++;
	

	return b;
}

STATIC void read_head(struct mpstr *mp)
{
	unsigned long head;

	head = read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);

	mp->header = head;
}

int decodeMP3(struct mpstr *mp,char *in,int isize,char *out,
		int osize,int *done)
{
	int len;

	gmp = mp;

	if(osize < 4608) {
		fprintf(stderr,"To less out space\n");
		return MP3_ERR;
	}

	if(in) {
		if(addbuf(mp,in,isize) == NULL) {
			return MP3_ERR;
		}
	}

	/* First decode header */
	if(mp->framesize == 0) {
		if(mp->bsize < 4) {
			return MP3_NEED_MORE;
		}
		read_head(mp);
		if (decode_header(&mp->fr,mp->header) == 0) return MP3_ERR;
		mp->framesize = mp->fr.framesize;
	}

	if(mp->fr.framesize > mp->bsize)
		return MP3_NEED_MORE;

	wordpointer = mp->bsspace[mp->bsnum] + 512;
	mp->bsnum = (mp->bsnum + 1) & 0x1;
	bitindex = 0;

	len = 0;
	while(len < mp->framesize) {
		int nlen;
		int blen = mp->tail->size - mp->tail->pos;
		if( (mp->framesize - len) <= blen) {
                  nlen = mp->framesize-len;
		}
		else {
                  nlen = blen;
                }
		memcpy(wordpointer+len,mp->tail->pnt+mp->tail->pos,nlen);
                len += nlen;
                mp->tail->pos += nlen;
		mp->bsize -= nlen;
                if(mp->tail->pos == mp->tail->size) {
                   remove_buf(mp);
                }
	}

	*done = 0;
	if(mp->fr.error_protection)
           getbits(16);

	if (mp->fr.lay != 3 ||
		do_layer3(&mp->fr,(unsigned char *) out,done) == 0) return MP3_ERR;

	mp->fsizeold = mp->framesize;
	mp->framesize = 0;

	return MP3_OK;
}

int set_pointer(long backstep)
{
  unsigned char *bsbufold;
  if(gmp->fsizeold < 0 && backstep > 0) {
    /* fprintf(stderr,"Can't step back %ld!\n",backstep); */
    return MP3_ERR;
  }
  bsbufold = gmp->bsspace[gmp->bsnum] + 512;
  wordpointer -= backstep;
  if (backstep)
    memcpy(wordpointer,bsbufold+gmp->fsizeold-backstep,backstep);
  bitindex = 0;
  return MP3_OK;
}