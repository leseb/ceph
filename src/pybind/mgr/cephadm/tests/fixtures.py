import time
try:
    from typing import Any
except ImportError:
    pass
import pytest

from cephadm import CephadmOrchestrator
from orchestrator import raise_if_exception, Completion
from tests import mock


def set_store(self, k, v):
    if v is None:
        del self._store[k]
    else:
        self._store[k] = v


def get_store(self, k):
    return self._store[k]


def get_store_prefix(self, prefix):
    return {
        k: v for k, v in self._store.items()
        if k.startswith(prefix)
    }

def get_ceph_option(_, key):
    return __file__

@pytest.yield_fixture()
def cephadm_module():
    with mock.patch("cephadm.module.CephadmOrchestrator.get_ceph_option", get_ceph_option),\
            mock.patch("cephadm.module.CephadmOrchestrator._configure_logging", lambda *args: None),\
            mock.patch("cephadm.module.CephadmOrchestrator.remote"),\
            mock.patch("cephadm.module.CephadmOrchestrator.set_store", set_store),\
            mock.patch("cephadm.module.CephadmOrchestrator.get_store", get_store),\
            mock.patch("cephadm.module.CephadmOrchestrator.get_store_prefix", get_store_prefix):
        CephadmOrchestrator._register_commands('')
        m = CephadmOrchestrator.__new__ (CephadmOrchestrator)
        m._root_logger = mock.MagicMock()
        m._store = {
            'ssh_config': '',
            'ssh_identity_key': '',
            'ssh_identity_pub': '',
            'inventory': {},
        }
        m.__init__('cephadm', 0, 0)
        yield m


def wait(m, c):
    # type: (CephadmOrchestrator, Completion) -> Any
    m.process([c])

    try:
        import pydevd  # if in debugger
        while True:    # don't timeout
            if c.is_finished:
                raise_if_exception(c)
                return c.result
            time.sleep(0.1)
    except ImportError:  # not in debugger
        for i in range(30):
            if i % 10 == 0:
                m.process([c])
            if c.is_finished:
                raise_if_exception(c)
                return c.result
            time.sleep(0.1)
    assert False, "timeout" + str(c._state)
