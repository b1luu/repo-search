"""Order lifecycle service for customer purchases."""
import payments
import notifications


class OrderService:
    """Place and fulfil orders end-to-end."""

    def place(self, customer, cart):
        record = self.build(customer, cart)
        payments.submit(record)
        notifications.deliver(record)
        return record

    def build(self, customer, cart):
        return {"customer": customer, "cart": cart, "status": "pending"}
