// GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT.
// generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2
import Foundation

public enum NexCoreFixtureMethod: String, CaseIterable {
    case echo = "echo"
    case ping = "ping"
}

@MainActor
public protocol NexCoreFixtureHandler {
    // direction: ui2js
    func echo(text: String) throws -> String
    // direction: js2ui
    func ping() throws -> String
}
