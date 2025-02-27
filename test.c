
#include "nostrdb.h"
#include "hex.h"
#include "io.h"
#include "protected_queue.h"
#include "memchr.h"
#include "bindings/c/profile_reader.h"
#include "bindings/c/profile_verifier.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static const char *test_dir = "./testdata/db";

static void test_load_profiles()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb *ndb;
	size_t mapsize;
	int written, ingester_threads;

	mapsize = 1024 * 1024 * 100;
	ingester_threads = 1;
	assert(ndb_init(&ndb, test_dir, mapsize, ingester_threads));

	read_file("testdata/profiles.json", (unsigned char*)json, alloc_size, &written);

	assert(ndb_process_events(ndb, json, written));

	ndb_destroy(ndb);

	assert(ndb_init(&ndb, test_dir, mapsize, ingester_threads));
	unsigned char id[32] = {
	  0x22, 0x05, 0x0b, 0x6d, 0x97, 0xbb, 0x9d, 0xa0, 0x9e, 0x90, 0xed, 0x0c,
	  0x6d, 0xd9, 0x5e, 0xed, 0x1d, 0x42, 0x3e, 0x27, 0xd5, 0xcb, 0xa5, 0x94,
	  0xd2, 0xb4, 0xd1, 0x3a, 0x55, 0x43, 0x09, 0x07 };
	const char *expected_content = "{\"website\":\"selenejin.com\",\"lud06\":\"\",\"nip05\":\"selenejin@BitcoinNostr.com\",\"picture\":\"https://nostr.build/i/3549697beda0fe1f4ae621f359c639373d92b7c8d5c62582b656c5843138c9ed.jpg\",\"display_name\":\"Selene Jin\",\"about\":\"INTJ | Founding Designer @Blockstream\",\"name\":\"SeleneJin\"}";

	struct ndb_note *note = ndb_get_note_by_id(ndb, id, NULL);
	assert(note != NULL);
	assert(!strcmp(ndb_note_content(note), expected_content));

	ndb_destroy(ndb);

	free(json);
	free(buf);
}

static void test_fuzz_events() {
	struct ndb *ndb;
	const char *str = "[\"EVENT\"\"\"{\"content\"\"created_at\":0 \"id\"\"5086a8f76fe1da7fb56a25d1bebbafd70fca62e36a72c6263f900ff49b8f8604\"\"kind\":0 \"pubkey\":9c87f94bcbe2a837adc28d46c34eeaab8fc2e1cdf94fe19d4b99ae6a5e6acedc \"sig\"\"27374975879c94658412469cee6db73d538971d21a7b580726a407329a4cafc677fb56b946994cea59c3d9e118fef27e4e61de9d2c46ac0a65df14153 ea93cf5\"\"tags\"[[][\"\"]]}]";

	ndb_init(&ndb, test_dir, 1024 * 1024, 1);
	ndb_process_event(ndb, str, strlen(str));
	ndb_destroy(ndb);
}

static void test_basic_event() {
	unsigned char buf[512];
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int ok;

	unsigned char id[32];
	memset(id, 1, 32);

	unsigned char pubkey[32];
	memset(pubkey, 2, 32);

	unsigned char sig[64];
	memset(sig, 3, 64);

	const char *hex_pk = "5d9b81b2d4d5609c5565286fc3b511dc6b9a1b3d7d1174310c624d61d1f82bb9";

	ok = ndb_builder_init(b, buf, sizeof(buf));
	assert(ok);
	note = builder.note;

	memset(note->padding, 3, sizeof(note->padding));

	ok = ndb_builder_set_content(b, hex_pk, strlen(hex_pk)); assert(ok);
	ndb_builder_set_id(b, id); assert(ok);
	ndb_builder_set_pubkey(b, pubkey); assert(ok);
	ndb_builder_set_sig(b, sig); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "p", 1); assert(ok);
	ok = ndb_builder_push_tag_str(b, hex_pk, 64); assert(ok);

	ok = ndb_builder_new_tag(b); assert(ok);
	ok = ndb_builder_push_tag_str(b, "word", 4); assert(ok);
	ok = ndb_builder_push_tag_str(b, "words", 5); assert(ok);
	ok = ndb_builder_push_tag_str(b, "w", 1); assert(ok);

	ok = ndb_builder_finalize(b, &note, NULL);
	assert(ok);

	// content should never be packed id
	assert(note->content.packed.flag != NDB_PACKED_ID);
	assert(note->tags.count == 2);

	// test iterator
	struct ndb_iterator iter, *it = &iter;
	
	ndb_tags_iterate_start(note, it);
	ok = ndb_tags_iterate_next(it);
	assert(ok);

	assert(it->tag->count == 2);
	const char *p      = ndb_iter_tag_str(it, 0).str;
	struct ndb_str hpk = ndb_iter_tag_str(it, 1);

	hex_decode(hex_pk, 64, id, 32);

	assert(hpk.flag == NDB_PACKED_ID);
	assert(memcmp(hpk.id, id, 32) == 0);
	assert(!strcmp(p, "p"));

	ok = ndb_tags_iterate_next(it);
	assert(ok);
	assert(it->tag->count == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1).str, "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2).str, "w"));

	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void test_empty_tags() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_iterator iter, *it = &iter;
	struct ndb_note *note;
	int ok;
	unsigned char buf[1024];

	ok = ndb_builder_init(b, buf, sizeof(buf));
	assert(ok);

	ok = ndb_builder_finalize(b, &note, NULL);
	assert(ok);

	assert(note->tags.count == 0);

	ndb_tags_iterate_start(note, it);
	ok = ndb_tags_iterate_next(it);
	assert(!ok);
}

static void print_tag(struct ndb_note *note, struct ndb_tag *tag) {
	for (int i = 0; i < tag->count; i++) {
		union ndb_packed_str *elem = &tag->strs[i];
		struct ndb_str str = ndb_note_str(note, elem);
		if (str.flag == NDB_PACKED_ID) {
			printf("<id> ");
		} else {
			printf("%s ", str.str);
		}
	}
	printf("\n");
}

static void test_parse_contact_list()
{
	int size, written = 0;
	unsigned char id[32];
	static const int alloc_size = 2 << 18;
	unsigned char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_note *note;

	read_file("testdata/contacts.json", json, alloc_size, &written);

	size = ndb_note_from_json((const char*)json, written, &note, buf, alloc_size);
	printf("ndb_note_from_json size %d\n", size);
	assert(size > 0);
	assert(size == 34328);

	memcpy(id, note->id, 32);
	memset(note->id, 0, 32);
	assert(ndb_calculate_id(note, json, alloc_size));
	assert(!memcmp(note->id, id, 32));

	const char* expected_content = 
	"{\"wss://nos.lol\":{\"write\":true,\"read\":true},"
	"\"wss://relay.damus.io\":{\"write\":true,\"read\":true},"
	"\"ws://monad.jb55.com:8080\":{\"write\":true,\"read\":true},"
	"\"wss://nostr.wine\":{\"write\":true,\"read\":true},"
	"\"wss://welcome.nostr.wine\":{\"write\":true,\"read\":true},"
	"\"wss://eden.nostr.land\":{\"write\":true,\"read\":true},"
	"\"wss://relay.mostr.pub\":{\"write\":true,\"read\":true},"
	"\"wss://nostr-pub.wellorder.net\":{\"write\":true,\"read\":true}}";

	assert(!strcmp(expected_content, ndb_note_content(note)));
	assert(ndb_note_created_at(note) == 1689904312);
	assert(ndb_note_kind(note) == 3);
	assert(note->tags.count == 786);
	//printf("note content length %d\n", ndb_note_content_length(note));
	printf("ndb_content_len %d, expected_len %ld\n",
			ndb_note_content_length(note),
			strlen(expected_content));
	assert(ndb_note_content_length(note) == strlen(expected_content));

	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it);

	int tags = 0;
	int total_elems = 0;

	while (ndb_tags_iterate_next(it)) {
		total_elems += it->tag->count;
		//printf("tag %d: ", tags);
		if (tags == 0 || tags == 1 || tags == 2)
			assert(it->tag->count == 3);

		if (tags == 6)
			assert(it->tag->count == 2);

		if (tags == 7)
			assert(!strcmp(ndb_note_str(note, &it->tag->strs[2]).str,
						"wss://nostr-pub.wellorder.net"));

		if (tags == 786) {
			static unsigned char h[] = { 0x74, 0xfa, 0xe6, 0x66, 0x4c, 0x9e, 0x79, 0x98, 0x0c, 0x6a, 0xc1, 0x1c, 0x57, 0x75, 0xed, 0x30, 0x93, 0x2b, 0xe9, 0x26, 0xf5, 0xc4, 0x5b, 0xe8, 0xd6, 0x55, 0xe0, 0x0e, 0x35, 0xec, 0xa2, 0x88 };
			assert(!memcmp(ndb_note_str(note, &it->tag->strs[1]).id, h, 32));
		}

		//print_tag(it->note, it->tag);

		tags += 1;
	}

	assert(tags == 786);
	//printf("total_elems %d\n", total_elems);
	assert(total_elems == 1580);

	write_file("test_contacts_ndb_note", (unsigned char *)note, size);
	printf("wrote test_contacts_ndb_note (raw ndb_note)\n");

	free(json);
	free(buf);
}

static void test_fetch_last_noteid()
{
	static const int alloc_size = 1024 * 1024;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb *ndb;
	size_t mapsize, len;
	int written, ingester_threads;

	mapsize = 1024 * 1024 * 100;
	ingester_threads = 1;
	assert(ndb_init(&ndb, test_dir, mapsize, ingester_threads));

	read_file("testdata/random.json", (unsigned char*)json, alloc_size, &written);
	assert(ndb_process_events(ndb, json, written));

	ndb_destroy(ndb);

	assert(ndb_init(&ndb, test_dir, mapsize, ingester_threads));

	unsigned char id[32] = { 0xdc, 0x96, 0x4f, 0x4c, 0x89, 0x83, 0x64, 0x13, 0x8e, 0x81, 0x96, 0xf0, 0xc7, 0x33, 0x38, 0xc8, 0xcc, 0x3e, 0xbf, 0xa3, 0xaf, 0xdd, 0xbc, 0x7d, 0xd1, 0x58, 0xb4, 0x84, 0x7c, 0x1e, 0xbf, 0xa0 };

	struct ndb_note *note = ndb_get_note_by_id(ndb, id, &len);
	assert(note != NULL);
	assert(note->created_at == 1650054135);
	
	unsigned char pk[32] = { 0x32, 0xe1, 0x82, 0x76, 0x35, 0x45, 0x0e, 0xbb, 0x3c, 0x5a, 0x7d, 0x12, 0xc1, 0xf8, 0xe7, 0xb2, 0xb5, 0x14, 0x43, 0x9a, 0xc1, 0x0a, 0x67, 0xee, 0xf3, 0xd9, 0xfd, 0x9c, 0x5c, 0x68, 0xe2, 0x45 };

	unsigned char profile_note_id[32] = {
		0xd1, 0x2c, 0x17, 0xbd, 0xe3, 0x09, 0x4a, 0xd3, 0x2f, 0x4a, 0xb8, 0x62, 0xa6, 0xcc, 0x6f, 0x5c, 0x28, 0x9c, 0xfe, 0x7d, 0x58, 0x02, 0x27, 0x0b, 0xdf, 0x34, 0x90, 0x4d, 0xf5, 0x85, 0xf3, 0x49
	};

	void *root = ndb_get_profile_by_pubkey(ndb, pk, &len);

	assert(root);
	int res = NdbProfileRecord_verify_as_root(root, len);
	printf("NdbProfileRecord verify result %d\n", res);
	assert(res == 0);

	NdbProfileRecord_table_t profile_record = NdbProfileRecord_as_root(root);
	NdbProfile_table_t profile = NdbProfileRecord_profile_get(profile_record);
	const char *lnurl = NdbProfileRecord_lnurl_get(profile_record);
	const char *name = NdbProfile_name_get(profile);
	uint64_t key = NdbProfileRecord_note_key_get(profile_record);
	assert(name);
	assert(lnurl);
	assert(!strcmp(name, "jb55"));
	assert(!strcmp(lnurl, "fixme"));

	printf("note_key %" PRIu64 "\n", key);

	struct ndb_note *n = ndb_get_note_by_key(ndb, key, NULL);
	assert(memcmp(profile_note_id, n->id, 32) == 0);

	//fwrite(profile, len, 1, stdout);

	ndb_destroy(ndb);

	free(json);
	free(buf);
}

static void test_parse_contact_event()
{
	int written;
	static const int alloc_size = 2 << 18;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_tce tce;

	assert(read_file("testdata/contacts-event.json", (unsigned char*)json,
			 alloc_size, &written));
	assert(ndb_ws_event_from_json(json, written, &tce, buf, alloc_size, NULL));

	assert(tce.evtype == NDB_TCE_EVENT);

	free(json);
	free(buf);
}

static void test_content_len()
{
	int written;
	static const int alloc_size = 2 << 18;
	char *json = malloc(alloc_size);
	unsigned char *buf = malloc(alloc_size);
	struct ndb_tce tce;

	assert(read_file("testdata/failed_size.json", (unsigned char*)json,
			 alloc_size, &written));
	assert(ndb_ws_event_from_json(json, written, &tce, buf, alloc_size, NULL));

	assert(tce.evtype == NDB_TCE_EVENT);
	assert(ndb_note_content_length(tce.event.note) == 0);

	free(json);
	free(buf);
}

static void test_parse_json() {
	char hex_id[32] = {0};
	unsigned char buffer[1024];
	struct ndb_note *note;
#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"
	static const char *json = 
		"{\"id\": \"" HEX_ID "\",\"pubkey\": \"" HEX_PK "\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [[\"p\",\"" HEX_ID "\"], [\"word\", \"words\", \"w\"]],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}";
	int ok;

	ok = ndb_note_from_json(json, strlen(json), &note, buffer, sizeof(buffer));
	assert(ok);

	const char *content = ndb_note_content(note);
	unsigned char *id = ndb_note_id(note);

	hex_decode(HEX_ID, 64, hex_id, sizeof(hex_id));

	assert(!strcmp(content, "共通語"));
	assert(!memcmp(id, hex_id, 32));

	assert(note->tags.count == 2);

	struct ndb_iterator iter, *it = &iter;
	ndb_tags_iterate_start(note, it); assert(ok);
	ok = ndb_tags_iterate_next(it); assert(ok);
	assert(it->tag->count == 2);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "p"));
	assert(!memcmp(ndb_iter_tag_str(it, 1).id, hex_id, 32));

	ok = ndb_tags_iterate_next(it); assert(ok);
	assert(it->tag->count == 3);
	assert(!strcmp(ndb_iter_tag_str(it, 0).str, "word"));
	assert(!strcmp(ndb_iter_tag_str(it, 1).str, "words"));
	assert(!strcmp(ndb_iter_tag_str(it, 2).str, "w"));
}

static void test_strings_work_before_finalization() {
	struct ndb_builder builder, *b = &builder;
	struct ndb_note *note;
	int ok;
	unsigned char buf[1024];

	ok = ndb_builder_init(b, buf, sizeof(buf)); assert(ok);
	ndb_builder_set_content(b, "hello", 5);

	assert(!strcmp(ndb_note_str(b->note, &b->note->content).str, "hello"));
	assert(ndb_builder_finalize(b, &note, NULL));

	assert(!strcmp(ndb_note_str(b->note, &b->note->content).str, "hello"));
}

static void test_tce_eose() {
	unsigned char buf[1024];
	const char json[] = "[\"EOSE\",\"s\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_EOSE);
	assert(tce.subid_len == 1);
	assert(!memcmp(tce.subid, "s", 1));
}

static void test_tce_command_result() {
	unsigned char buf[1024];
	const char json[] = "[\"OK\",\"\",true,\"blocked: ok\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_OK);
	assert(tce.subid_len == 0);
	assert(tce.command_result.ok == 1);
	assert(!memcmp(tce.subid, "", 0));
}

static void test_tce_command_result_empty_msg() {
	unsigned char buf[1024];
	const char json[] = "[\"OK\",\"b1d8f68d39c07ce5c5ea10c235100d529b2ed2250140b36a35d940b712dc6eff\",true,\"\"]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_OK);
	assert(tce.subid_len == 64);
	assert(tce.command_result.ok == 1);
	assert(tce.command_result.msglen == 0);
	assert(!memcmp(tce.subid, "b1d8f68d39c07ce5c5ea10c235100d529b2ed2250140b36a35d940b712dc6eff", 0));
}

// test to-client event
static void test_tce() {

#define HEX_ID "5004a081e397c6da9dc2f2d6b3134006a9d0e8c1b46689d9fe150bb2f21a204d"
#define HEX_PK "b169f596968917a1abeb4234d3cf3aa9baee2112e58998d17c6db416ad33fe40"
#define JSON "{\"id\": \"" HEX_ID "\",\"pubkey\": \"" HEX_PK "\",\"created_at\": 1689836342,\"kind\": 1,\"tags\": [[\"p\",\"" HEX_ID "\"], [\"word\", \"words\", \"w\"]],\"content\": \"共通語\",\"sig\": \"e4d528651311d567f461d7be916c37cbf2b4d530e672f29f15f353291ed6df60c665928e67d2f18861c5ca88\"}"
	unsigned char buf[1024];
	const char json[] = "[\"EVENT\",\"subid123\"," JSON "]";
	struct ndb_tce tce;
	int ok;

	ok = ndb_ws_event_from_json(json, sizeof(json), &tce, buf, sizeof(buf), NULL);
	assert(ok);

	assert(tce.evtype == NDB_TCE_EVENT);
	assert(tce.subid_len == 8);
	assert(!memcmp(tce.subid, "subid123", 8));

#undef HEX_ID
#undef HEX_PK
#undef JSON
}

#define TEST_BUF_SIZE 10  // For simplicity

static void test_queue_init_pop_push() {
	struct prot_queue q;
	int buffer[TEST_BUF_SIZE];
	int data;

	// Initialize
	assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

	// Push and Pop
	data = 5;
	assert(prot_queue_push(&q, &data) == 1);
	prot_queue_pop(&q, &data);
	assert(data == 5);

	// Push to full, and then fail to push
	for (int i = 0; i < TEST_BUF_SIZE; i++) {
		assert(prot_queue_push(&q, &i) == 1);
	}
	assert(prot_queue_push(&q, &data) == 0);  // Should fail as queue is full

	// Pop to empty, and then fail to pop
	for (int i = 0; i < TEST_BUF_SIZE; i++) {
		assert(prot_queue_try_pop(&q, &data) == 1);
		assert(data == i);
	}
	assert(prot_queue_try_pop(&q, &data) == 0);  // Should fail as queue is empty
}

// This function will be used by threads to test thread safety.
void* thread_func(void* arg) {
	struct prot_queue* q = (struct prot_queue*) arg;
	int data;

	for (int i = 0; i < 100; i++) {
		data = i;
		prot_queue_push(q, &data);
		prot_queue_pop(q, &data);
	}
	return NULL;
}

static void test_queue_thread_safety() {
	struct prot_queue q;
	int buffer[TEST_BUF_SIZE];
	pthread_t threads[2];

	assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

	// Create threads
	for (int i = 0; i < 2; i++) {
		pthread_create(&threads[i], NULL, thread_func, &q);
	}

	// Join threads
	for (int i = 0; i < 2; i++) {
		pthread_join(threads[i], NULL);
	}

	// After all operations, the queue should be empty
	int data;
	assert(prot_queue_try_pop(&q, &data) == 0);
}

static void test_queue_boundary_conditions() {
    struct prot_queue q;
    int buffer[TEST_BUF_SIZE];
    int data;

    // Initialize
    assert(prot_queue_init(&q, buffer, sizeof(buffer), sizeof(int)) == 1);

    // Push to full
    for (int i = 0; i < TEST_BUF_SIZE; i++) {
        assert(prot_queue_push(&q, &i) == 1);
    }

    // Try to push to a full queue
    int old_head = q.head;
    int old_tail = q.tail;
    int old_count = q.count;
    assert(prot_queue_push(&q, &data) == 0);
    
    // Assert the queue's state has not changed
    assert(old_head == q.head);
    assert(old_tail == q.tail);
    assert(old_count == q.count);

    // Pop to empty
    for (int i = 0; i < TEST_BUF_SIZE; i++) {
        assert(prot_queue_try_pop(&q, &data) == 1);
    }

    // Try to pop from an empty queue
    old_head = q.head;
    old_tail = q.tail;
    old_count = q.count;
    assert(prot_queue_try_pop(&q, &data) == 0);
    
    // Assert the queue's state has not changed
    assert(old_head == q.head);
    assert(old_tail == q.tail);
    assert(old_count == q.count);
}

static void test_fast_strchr()
{
	// Test 1: Basic test
	const char *testStr1 = "Hello, World!";
	assert(fast_strchr(testStr1, 'W', strlen(testStr1)) == testStr1 + 7);

	// Test 2: Character not present in the string
	assert(fast_strchr(testStr1, 'X', strlen(testStr1)) == NULL);

	// Test 3: Multiple occurrences of the character
	const char *testStr2 = "Multiple occurrences.";
	assert(fast_strchr(testStr2, 'u', strlen(testStr2)) == testStr2 + 1);

	// Test 4: Check with an empty string
	const char *testStr3 = "";
	assert(fast_strchr(testStr3, 'a', strlen(testStr3)) == NULL);

	// Test 5: Check with a one-character string
	const char *testStr4 = "a";
	assert(fast_strchr(testStr4, 'a', strlen(testStr4)) == testStr4);

	// Test 6: Check the last character in the string
	const char *testStr5 = "Last character check";
	assert(fast_strchr(testStr5, 'k', strlen(testStr5)) == testStr5 + 19);

	// Test 7: Large string test (>16 bytes)
	char *testStr6 = "This is a test for large strings with more than 16 bytes.";
	assert(fast_strchr(testStr6, 'm', strlen(testStr6)) == testStr6 + 38);
}

int main(int argc, const char *argv[]) {
	test_basic_event();
	test_empty_tags();
	test_parse_json();
	test_parse_contact_list();
	test_strings_work_before_finalization();
	test_tce();
	test_tce_command_result();
	test_tce_eose();
	test_tce_command_result_empty_msg();
	test_content_len();
	test_fuzz_events();

	// note fetching
	test_fetch_last_noteid();

	// protected queue tests
	test_queue_init_pop_push();
	test_queue_thread_safety();
	test_queue_boundary_conditions();

	// memchr stuff
	test_fast_strchr();

	// profiles
	test_load_profiles();

	printf("All tests passed!\n");       // Print this if all tests pass.
}



