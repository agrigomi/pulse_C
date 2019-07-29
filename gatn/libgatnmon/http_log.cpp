#include "iGatn.h"
#include "iArgs.h"
#include "iLog.h"
#include "iRepository.h"

#define MAX_EVENTS	16

typedef struct {
	_on_http_event_t	*p_cb;
	void			*udata;
}_http_evt_t;

class cHttpLog: public iGatnExtension {
private:
	iArgs		*mpi_args;
	iLog		*mpi_log;
	_http_evt_t	m_original_evt[MAX_EVENTS]; // backup events

	void backup_handlers(_server_t *psrv, _cstr_t host) {
		m_original_evt[ON_REQUEST].p_cb = psrv->get_event_handler(ON_REQUEST, &(m_original_evt[ON_REQUEST].udata), host);
		m_original_evt[ON_ERROR].p_cb = psrv->get_event_handler(ON_ERROR, &(m_original_evt[ON_ERROR].udata), host);
		m_original_evt[ON_NOT_FOUND].p_cb = psrv->get_event_handler(ON_NOT_FOUND, &(m_original_evt[ON_NOT_FOUND].udata), host);
	}

	void set_handlers(_server_t *psrv, _cstr_t host) {
		psrv->on_event(ON_REQUEST, [](iHttpServerConnection *pscnt, void *udata) {
			cHttpLog *pobj = (cHttpLog *)udata;
			iLog *pi_log = pobj->mpi_log;
			_cstr_t method = pscnt->req_var(VAR_REQ_METHOD);
			_cstr_t uri = pscnt->req_var(VAR_REQ_URI);
			_cstr_t protocol = pscnt->req_var(VAR_REQ_PROTOCOL);

			//...
			pobj->call_original_handler(ON_REQUEST, pscnt);
		}, this, host);

		psrv->on_event(ON_ERROR, [](iHttpServerConnection *pscnt, void *udata) {
			cHttpLog *pobj = (cHttpLog *)udata;
			iLog *pi_log = pobj->mpi_log;

			//...
			pobj->call_original_handler(ON_ERROR, pscnt);
		}, this, host);

		psrv->on_event(ON_NOT_FOUND, [](iHttpServerConnection *pscnt, void *udata) {
			cHttpLog *pobj = (cHttpLog *)udata;
			iLog *pi_log = pobj->mpi_log;

			//...
			pobj->call_original_handler(ON_NOT_FOUND, pscnt);
		}, this, host);
	}

	void restore_handlers(_server_t *psrv, _cstr_t host) {
		psrv->on_event(ON_REQUEST, m_original_evt[ON_REQUEST].p_cb, m_original_evt[ON_REQUEST].udata, host);
		psrv->on_event(ON_ERROR, m_original_evt[ON_ERROR].p_cb, m_original_evt[ON_ERROR].udata, host);
		psrv->on_event(ON_NOT_FOUND, m_original_evt[ON_NOT_FOUND].p_cb, m_original_evt[ON_NOT_FOUND].udata, host);
	}

	void call_original_handler(_u8 evt, iHttpServerConnection *pscnt) {
		if(evt < MAX_EVENTS) {
			if(m_original_evt[evt].p_cb)
				m_original_evt[evt].p_cb(pscnt, m_original_evt[evt].udata);
		}
	}
public:
	BASE(cHttpLog, "cHttpLog", RF_CLONE, 1,0,0);

	bool object_ctl(_u32 cmd, void *arg, ...) {
		bool r = false;

		switch(cmd) {
			case OCTL_INIT:
				memset(&m_original_evt, 0, sizeof(_http_evt_t));

				mpi_args = dynamic_cast<iArgs *>(_gpi_repo_->object_by_iname(I_ARGS, RF_CLONE|RF_NONOTIFY));
				mpi_log = dynamic_cast<iLog *>(_gpi_repo_->object_by_iname(I_LOG, RF_ORIGINAL));

				if(mpi_args && mpi_log)
					r = true;
				break;
			case OCTL_UNINIT:
				_gpi_repo_->object_release(mpi_args, false);
				_gpi_repo_->object_release(mpi_log, false);
				r = true;
				break;
		}

		return r;
	}

	bool options(_cstr_t opt) {
		bool r = false;

		if(opt)
			r = mpi_args->init(opt, strlen(opt));

		return r;
	}

	bool attach(_server_t *p_srv, _cstr_t host=NULL) {
		bool r = false;

		backup_handlers(p_srv, host);
		set_handlers(p_srv, host);

		return r;
	}

	void detach(_server_t *p_srv, _cstr_t host=NULL) {
		restore_handlers(p_srv, host);
	}
};

static cHttpLog _g_http_log_;
