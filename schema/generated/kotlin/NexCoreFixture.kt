// GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT.
// generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2
package nexcore.fixture

enum class NexCoreFixtureMethod(val wire: String) {
    echo("echo"),
    ping("ping")
}

interface NexCoreFixtureHandler {
    // direction: ui2js
    fun echo(text: String): String
    // direction: js2ui
    fun ping(): String
}
