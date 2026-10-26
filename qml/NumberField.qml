import QtQuick
import md3.Core as MD

// Compose numeric editing with the upstream TextField, without replacing its visuals.
MD.TextField {
    id: field
    property int value: 0
    property int from: 0
    property int to: 100
    signal valueModified(int value)
    type: "outlined"
    validator: IntValidator {bottom: field.from; top: field.to}
    inputMethodHints: Qt.ImhDigitsOnly
    Binding {target: field; property: "text"; value: String(field.value); when: !field.focused}
    function commit() {
        let parsed = Number(text)
        let next = text.trim().length && Number.isFinite(parsed) ? Math.round(parsed) : value
        next = Math.max(from, Math.min(to, next))
        valueModified(next)
        text = String(next)
    }
    function stepBy(amount) {valueModified(Math.max(from, Math.min(to, value + amount)))}
    onTextChanged: {
        if (focused && /^\d+$/.test(text)) {
            let parsed = Number(text)
            if (parsed >= from && parsed <= to) valueModified(parsed)
        }
    }
    onEditingFinished: commit()
    onAccepted: commit()
}
