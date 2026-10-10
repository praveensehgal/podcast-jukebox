import pytest

from jukebox_resolver.guard import is_http_url, is_public_ip

PUBLIC = "93.184.216.34"


@pytest.mark.parametrize(
    ("ip", "public"),
    [
        (PUBLIC, True),
        ("2606:4700::1111", True),
        ("127.0.0.1", False),
        ("10.0.0.1", False),
        ("172.31.255.255", False),
        ("192.168.0.1", False),
        ("169.254.169.254", False),
        ("100.127.0.1", False),
        ("224.0.0.1", False),
        ("::1", False),
        ("fe80::1%en0", False),
        ("fd00::1", False),
        ("::ffff:192.168.1.1", False),
        ("not-an-ip", False),
    ],
)
def test_is_public_ip(ip, public):
    assert is_public_ip(ip) is public


def test_is_http_url():
    assert is_http_url("https://a.b/c")
    assert is_http_url("http://a:80")
    assert not is_http_url("https://a:8443/")
    assert not is_http_url("ftp://a")
    assert not is_http_url("https://")
    assert not is_http_url("not a url")
