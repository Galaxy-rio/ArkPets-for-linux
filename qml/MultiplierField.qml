import QtQuick
import md3.Core as MD

MD.TextField {
    id: field
    property real value: 1
    property real from: 0.25
    property real to: 2.5
    property int decimals: 2
    signal valueModified(real value)
    type: "outlined"
    label: "倍"
    validator: DoubleValidator {bottom: field.from; top: field.to; decimals: field.decimals; notation: DoubleValidator.StandardNotation; locale: "C"}
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    Binding {target: field; property: "text"; value: String(field.value); when: !field.focused}
    function commit() {
        let parsed = Number(text)
        let next = text.trim().length && Number.isFinite(parsed) ? parsed : value
        let precision = Math.pow(10, decimals)
        next = Math.round(Math.max(from, Math.min(to, next)) * precision) / precision
        valueModified(next)
        text = String(next)
    }
    onTextChanged: {
        if (focused && new RegExp("^\\d*\\.?\\d{0," + decimals + "}$").test(text)) {
            let parsed = Number(text)
            if (parsed >= from && parsed <= to) valueModified(parsed)
        }
    }
    onEditingFinished: commit()
    onAccepted: commit()
}
