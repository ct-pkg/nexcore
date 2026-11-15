// GENERATED FROM tests/fixture.schema.yaml -- DO NOT EDIT.
// generator: nexcore-gen-schema 0.1.0  schema_hash: f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2

export const schemaHash = "f7554c34c7bf46f8a896a76a779a96c5b1eedc29ce93539de2bebd6b7c4a45b2";

export const methods = Object.freeze({
  echo: Object.freeze({ direction: "ui2js", result: "string", params: Object.freeze(["text"]) }),
  ping: Object.freeze({ direction: "js2ui", result: "string", params: Object.freeze([]) })
});

export function createHandler(impl) {
  return Object.freeze({
    echo(text) { return impl.echo(text); },
    ping() { return impl.ping(); }
  });
}
