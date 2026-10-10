from jukebox_resolver.store import Store


def test_position_upsert_and_list(tmp_path):
    store = Store(str(tmp_path / "s.db"))
    assert store.get_position("dev1", "ep1") is None
    row = store.upsert_position("dev1", "ep1", 120, 3600)
    assert row["pos_s"] == 120 and row["duration_s"] == 3600
    assert row["updated"].endswith("Z")
    row = store.upsert_position("dev1", "ep1", 300, None)
    assert row["pos_s"] == 300
    assert row["duration_s"] == 3600  # null duration keeps the previous value
    store.upsert_position("dev1", "ep2", 5, 10)
    store.upsert_position("dev2", "ep1", 7, None)
    rows = store.list_positions("dev1")
    assert {r["episode"] for r in rows} == {"ep1", "ep2"}
    assert set(rows[0]) == {"episode", "pos_s", "duration_s", "updated"}
    assert store.list_positions("nobody") == []
    store.close()


def test_feed_cache_roundtrip(tmp_path):
    store = Store(str(tmp_path / "s.db"))
    assert store.get_feed("f") is None
    eps = [{"id": "a", "title": "t", "size": None}]
    at = store.put_feed("f", eps)
    got = store.get_feed("f")
    assert got is not None
    assert got[0] == at and got[1] == eps
    store.put_feed("f", [])
    assert store.get_feed("f")[1] == []
    store.close()


def test_kv_roundtrip(tmp_path):
    store = Store(str(tmp_path / "s.db"))
    assert store.get_kv("k") is None
    store.put_kv("k", "v1")
    store.put_kv("k", "v2")
    got = store.get_kv("k")
    assert got is not None and got[1] == "v2"
    store.close()
