"""Outbound notification dispatch for customer orders."""


def deliver(record):
    """Deliver an outbound notification to the user."""
    return {"to": record.get("customer"), "kind": "order_update"}
