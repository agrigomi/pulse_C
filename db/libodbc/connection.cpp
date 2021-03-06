#include <string.h>
#include "private.h"

bool dbc::init(_cstr_t connect_string) {
	bool r = false;

	mpi_log = (iLog *)_gpi_repo_->object_by_iname(I_LOG, RF_ORIGINAL);

	/* Allocate an environment handle */
	if(SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_henv))) {
		/* We want ODBC 3 support */
		SQLSetEnvAttr(m_henv, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);
		/* Allocate a connection handle */
		if(SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, m_henv, &m_hdbc))) {
			/* Connect to the ODBC driver */
			if(SQL_SUCCEEDED(SQLDriverConnect(m_hdbc, NULL, (SQLCHAR *)connect_string, SQL_NTS,
			                 0, 0, 0, SQL_DRIVER_COMPLETE))) {
				if((mpi_stmt_pool = (iPool *)_gpi_repo_->object_by_iname(I_POOL, RF_CLONE))) {
					if((r = mpi_stmt_pool->init(sizeof(sql), [](_u8 op, void *data, void *udata) {
						sql *psql = (sql *)data;
						dbc *pdbc = (dbc *)udata;

						switch(op) {
							case POOL_OP_NEW: {
									/* Needed for the virtual table */
									sql tmp;

									memcpy((void *)psql, &tmp, sizeof(sql));
									/********************************/
									psql->_init(pdbc);
								} break;
							case POOL_OP_BUSY:
								break;
							case POOL_OP_FREE:
								psql->_free();
								break;
							case POOL_OP_DELETE:
								psql->_destroy();
								break;
						}
					}, this))) {
						/* Get driver info for STMT limit */
						SQLGetInfo(m_hdbc, SQL_MAX_CONCURRENT_ACTIVITIES, &m_stmt_limit, 0, 0);
						if(m_stmt_limit == 0)
							m_stmt_limit = 0xffff;
					}
				}
			} else {
				mpi_log->fwrite(LMT_ERROR, "ODBC: Unable to connect '%s'", connect_string);
				diagnostics([](_cstr_t state, _u32 native, _cstr_t text, void *udata) {
					dbc *pdbc = (dbc *)udata;

					pdbc->mpi_log->fwrite(LMT_ERROR, "ODBC: %s, %u, %s", state, native, text);
				}, this);
			}
		} else
			mpi_log->write(LMT_ERROR, "ODBC: Unable to allocate connection handle");
	} else
		mpi_log->write(LMT_ERROR, "ODBC: Unable to allocate environment handle");

	if(!r)
		destroy();

	return r;
}

void dbc::destroy(void) {
	if(mpi_stmt_pool) {
		_gpi_repo_->object_release(mpi_stmt_pool);
		mpi_stmt_pool = NULL;
	}

	if(m_hdbc) {
		/* Disconnect from driver */
		SQLDisconnect(m_hdbc);

		/* Deallocate connection handle */
		SQLFreeHandle(SQL_HANDLE_DBC, m_hdbc);
		m_hdbc = NULL;
	}

	if(m_henv) {
		/* Deallocate environment handle */
		SQLFreeHandle(SQL_HANDLE_ENV, m_henv);
		m_henv = NULL;
	}

	if(mpi_log) {
		_gpi_repo_->object_release(mpi_log);
		mpi_log = NULL;
	}
}

sql *dbc::alloc(void) {
	sql *r = NULL;

	if(mpi_stmt_pool && m_hdbc && m_stmt_count < m_stmt_limit) {
		if((r = (sql *)mpi_stmt_pool->alloc()))
			m_stmt_count++;
	}

	return r;
}

void dbc::free(sql *psql) {
	if(mpi_stmt_pool) {
		mpi_stmt_pool->free(psql);
		m_stmt_count--;
	}
}

void dbc::diagnostics(void (*pcb)(_cstr_t state, _u32 native, _cstr_t text, void *udata), void *udata) {
	SQLCHAR _state[16] = "";
	SQLCHAR _text[1024] = "";
	SQLINTEGER _native = 0;
	SQLSMALLINT _len = 0;
	_u32 i = 1;

	if(m_hdbc) {
		while(SQLGetDiagRec(SQL_HANDLE_DBC, m_hdbc, i,
				_state, &_native,
				_text, sizeof(_text), &_len) == SQL_SUCCESS) {
			pcb((_cstr_t)_state, _native, (_cstr_t)_text, udata);
			i++;
		}
	}
}
