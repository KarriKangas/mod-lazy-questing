param(
    [string]$Uri = 'http://127.0.0.1:7878/'
)

$soapUser = Read-Host 'SOAP user'
$soapPassword = Read-Host 'SOAP password' -AsSecureString
$soapCredential = [pscredential]::new($soapUser, $soapPassword)
$soapBody = @'
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:AC">
  <SOAP-ENV:Body>
    <ns1:executeCommand>
      <command>strictbots aquarium roster</command>
    </ns1:executeCommand>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
'@

$aquariumUri = [uri]$Uri
if (-not $aquariumUri.IsLoopback) {
    throw 'The Aquarium collector permits unencrypted authentication only on a loopback URI.'
}

$soapResponse = Invoke-WebRequest `
    -Uri $Uri `
    -Method Post `
    -Credential $soapCredential `
    -AllowUnencryptedAuthentication `
    -ContentType 'text/xml; charset=utf-8' `
    -Body $soapBody

[xml]$soapXml = $soapResponse.Content
$resultNode = $soapXml.SelectSingleNode("//*[local-name()='result']")
if (-not $resultNode) {
    throw 'SOAP response did not contain a result node.'
}

$rosterJson = $resultNode.InnerText -replace '^ALTBO_JSON\s+', ''
$roster = ($rosterJson.Trim() | ConvertFrom-Json).bots
$roster | ConvertTo-Json -Depth 8
