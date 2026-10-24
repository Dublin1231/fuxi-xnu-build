"""Negative controls for actual-image deferred-constructor admission checks."""
import unittest
from test_touch_link import fixture
from hid_service import audit_service

class ServiceAuditTests(unittest.TestCase):
    def audit(self,**changes):
        raw=fixture(service=True,**changes)
        return audit_service(raw,raw,[fixture(kind=1,service=True)],set())
    def test_deferred_hid_and_closed_service_gate(self):
        self.assertTrue(self.audit()['hid_constructor_deferred'])
    def test_early_hid_registration_rejected(self):
        with self.assertRaises(ValueError):self.audit(hid_early=True)
    def test_open_service_policy_rejected(self):
        with self.assertRaises(ValueError):self.audit(policy_open=True)

if __name__=='__main__':unittest.main()
