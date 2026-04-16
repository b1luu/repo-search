"""Upstream payment gateway adapter."""


def call(action, record):
    """Issue an HTTPS call to the gateway."""
    return {"action": action, "record": record}
