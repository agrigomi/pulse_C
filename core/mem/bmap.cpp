// buffer map
#include "iMemory.h"
#include "iRepository.h"

#define BFREE	0
#define BBUSY	1
#define BDIRTY	2

typedef struct __attribute__((packed)) {
	_u8	state;
	void	*ptr;
	void	*udata;
} _buffer_t;

class cBufferMap: public iBufferMap {
private:
	iLlist	*mpi_list;
	iHeap	*mpi_heap;
	volatile _u32	m_bsize;
	volatile _buffer_io_t *m_pcb_bio; // I/O callback

	void uninit_llist(_u8 col, HMUTEX hlock) {
		if(mpi_list) {
			_u32 sz = 0;

			// select column
			mpi_list->col(col, hlock);

			_buffer_t *b = (_buffer_t *)mpi_list->first(&sz, hlock);

			while(b) {
				if(m_pcb_bio)
					m_pcb_bio(BIO_UNINIT, b->ptr, m_bsize, b->udata);
				mpi_heap->free(b->ptr, m_bsize);
				mpi_list->del(hlock);
				b = (_buffer_t *)mpi_list->current(&sz, hlock);
			}
		}
	}

public:
	BASE(cBufferMap, "cBufferMap", RF_CLONE, 1,0,0);

	bool object_ctl(_u32 cmd, void *arg, ...) {
		bool r = false;

		switch(cmd) {
			case OCTL_INIT: {
				m_bsize = 0;
				m_pcb_bio = 0;
				iRepository *pi_repo = (iRepository *)arg;

				mpi_heap = dynamic_cast<iHeap *>(pi_repo->object_by_iname(I_HEAP, RF_ORIGINAL));
				mpi_list = dynamic_cast<iLlist *>(pi_repo->object_by_iname(I_LLIST, RF_CLONE));
				if(mpi_list && mpi_heap) {
					mpi_list->init(LL_VECTOR, 3);
					r = true;
				}
			} break;
			case OCTL_UNINIT: {
				iRepository *pi_repo = (iRepository *)arg;

				uninit();
				pi_repo->object_release(mpi_list);
				pi_repo->object_release(mpi_heap);
				mpi_list = 0;
				r = true;
			} break;
		}

		return r;
	}

	void init(_u32 buffer_size, _buffer_io_t *pcb_bio) {
		if(!m_bsize) {
			m_bsize = buffer_size;
			m_pcb_bio = pcb_bio;
		}
	}

	void uninit(void) {
		if(m_bsize) {
			HMUTEX hm = mpi_list->lock();

			uninit_llist(BFREE, hm);
			uninit_llist(BBUSY, hm);
			uninit_llist(BDIRTY, hm);

			m_bsize = 0;
			m_pcb_bio = 0;

			mpi_list->unlock(hm);
		}
	}

	HBUFFER alloc(void *udata=0) {
		HBUFFER r = 0;
		HMUTEX hm = mpi_list->lock();

		if(m_bsize) {
			_u32 sz = 0;

			// select FREE column
			mpi_list->col(BFREE, hm);

			_buffer_t *pb = (_buffer_t *)mpi_list->first(&sz, hm);

			if(pb) {
				// move to BUSY column
				if(mpi_list->mov(pb, BBUSY, hm)) {
					pb->state = BBUSY;
					pb->udata = udata;
					if(m_pcb_bio)
						m_pcb_bio(BIO_INIT, pb->ptr, m_bsize, pb->udata);
					r = pb;
				}
			} else {
				// alloc new buffer
				_buffer_t b;

				if((b.ptr = mpi_heap->alloc(m_bsize))) {
					b.state = BBUSY;
					b.udata = udata;
					mpi_list->col(BBUSY, hm);
					r = mpi_list->add(&b, sizeof(_buffer_t), hm);
					if(m_pcb_bio)
						m_pcb_bio(BIO_INIT, b.ptr, m_bsize, b.udata);
				}
			}
		}

		mpi_list->unlock(hm);

		return r;
	}

	void free(HBUFFER hb) {
		_buffer_t *b = (_buffer_t *)hb;
		HMUTEX hm = mpi_list->lock();

		if(m_bsize) {
			if(b->state == BDIRTY) {
				if(m_pcb_bio)
					m_pcb_bio(BIO_WRITE, b->ptr, m_bsize, b->udata);
			}
			if(m_pcb_bio)
				m_pcb_bio(BIO_UNINIT, b->ptr, m_bsize, b->udata);
			if(mpi_list->mov(b, BFREE, hm))
				b->state = BFREE;
		}

		mpi_list->unlock(hm);
	}

	void *ptr(HBUFFER hb) {
		_buffer_t *b = (_buffer_t *)hb;
		return b->ptr;
	}

	void dirty(HBUFFER hb) {
		_buffer_t *b = (_buffer_t *)hb;
		HMUTEX hm = mpi_list->lock();

		if(m_bsize) {
			if(mpi_list->mov(b, BDIRTY, hm))
				b->state = BDIRTY;
		}

		mpi_list->unlock(hm);
	}

	void *get_udata(HBUFFER hb) {
		_buffer_t *b = (_buffer_t *)hb;
		return b->udata;
	}

	_u32 get_size(void) {
		return m_bsize;
	}

	void set_udata(HBUFFER hb, void *udata) {
		_buffer_t *b = (_buffer_t *)hb;
		b->udata = udata;
	}

	void reset(HBUFFER hb=0) {
		_buffer_t *b = (_buffer_t *)hb;
		HMUTEX hm = mpi_list->lock();

		if(m_bsize) {
			if(b) {
				// reset passed buffer only
				if(b->state != BFREE) {
					if(m_pcb_bio)
						m_pcb_bio(BIO_READ, b->ptr, m_bsize, b->udata);
				}
				if(b->state == BDIRTY) {
					if(mpi_list->mov(b, BBUSY, hm))
						b->state = BBUSY;
				}
			} else {
				_u32 sz = 0;

				// select dirty column
				mpi_list->col(BDIRTY, hm);

				_buffer_t *b = (_buffer_t *)mpi_list->first(&sz, hm);

				while(b) {
					if(m_pcb_bio)
						m_pcb_bio(BIO_READ, b->ptr, m_bsize, b->udata);
					if(mpi_list->mov(b, BBUSY, hm)) {
						b->state = BBUSY;
						b = (_buffer_t *)mpi_list->current(&sz, hm);
					} else
						b = (_buffer_t *)mpi_list->next(&sz, hm);
				}
			}
		}

		mpi_list->unlock(hm);
	}

	void flush(HBUFFER hb=0) {
		_buffer_t *b = (_buffer_t *)hb;
		HMUTEX hm = mpi_list->lock();

		if(m_bsize) {
			if(b) {
				// commit passed buffer only
				if(b->state != BFREE) {
					if(m_pcb_bio)
						m_pcb_bio(BIO_WRITE, b->ptr, m_bsize, b->udata);
				}
				if(b->state == BDIRTY) {
					if(mpi_list->mov(b, BBUSY, hm))
						b->state = BBUSY;
				}
			} else {
				_u32 sz = 0;

				// select dirty column
				mpi_list->col(BDIRTY, hm);

				_buffer_t *b = (_buffer_t *)mpi_list->first(&sz, hm);

				while(b) {
					if(m_pcb_bio)
						m_pcb_bio(BIO_WRITE, b->ptr, m_bsize, b->udata);
					if(mpi_list->mov(b, BBUSY, hm)) {
						b->state = BBUSY;
						b = (_buffer_t *)mpi_list->current(&sz, hm);
					} else
						b = (_buffer_t *)mpi_list->next(&sz, hm);
				}
			}
		}

		mpi_list->unlock(hm);
	}
};

static cBufferMap _g_buffer_map_;
