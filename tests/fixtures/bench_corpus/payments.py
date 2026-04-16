"""Payment capture, refund, and settlement logic."""
import gateway


class PaymentProcessor:
    """Submit a charge through the upstream gateway."""

    def submit(self, record):
        return gateway.call("charge", record)

    def refund(self, record):
        return gateway.call("refund", record)
