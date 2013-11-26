<?php
/*
 * Copyright 2008 Develer S.r.l. (http://www.develer.com/)
 * All rights reserved.
 *
 * $Id:$
 * Author: Matteo Bertini <naufraghi@develer.com>
 */

/*
SELECT userid, firstname, lastname, activitydate, time_contract, SUM(time_worked) as time_worked,
    SUM(time_malattia) as time_malattia, SUM(time_ferie) as time_ferie, SUM(time_d20) as time_d20,
    SUM(tickets) as tickets
    FROM (
        SELECT userid, firstname, lastname, activitydate, time_contract,
            (NOT (malattia OR ferie OR d20)) * SUM(time_spent)/60 AS time_worked,
            malattia * SUM(time_spent)/60 AS time_malattia,
            ferie * SUM(time_spent)/60 AS time_ferie,
            d20 * SUM(time_spent)/60 AS time_d20,
            (NOT (malattia OR ferie OR d20)) * SUM(time_spent > 5*60) AS tickets
            FROM (
                SELECT person.userid, person.firstname, person.lastname,
                    usercontract.uc_hours/5 as time_contract, project.name AS project_name,
                    phase.name AS phase_name, hours.activitydate, sum(hours.time) AS time_spent,
                    (project.name = '__dipendenti' AND phase.name = 'malattia') AS malattia,
                    (project.name = '__dipendenti' AND phase.name = 'ferie') AS ferie,
                    (project.name LIKE 'd20_%') AS d20
                FROM hours
                    JOIN person ON hours.userid = person.id
                    JOIN phase ON hours.phaseid = phase.id
                    JOIN activity ON hours.activityid = activity.id
                    JOIN project ON phase.projectid = project.id
                    JOIN usercontract ON usercontract.userid = person.id
                    WHERE activitydate > '2008-08-05'
                GROUP BY person.firstname, person.lastname, person.userid, usercontract.uc_hours,
                         hours.activitydate, malattia, ferie, d20
            ) AS lavoro
        GROUP BY userid, firstname, lastname, activitydate, time_contract, malattia, ferie, d20
    ) AS parts
GROUP BY userid, firstname, lastname, activitydate, time_contract
*/

useattrib("atkdateattribute");
atkImport("modules.scheduler.scheduler_holidays");
atkImport("modules.reports.weekreport");
atkimport("module.timereg.locking");

setlocale(LC_ALL, 'it_IT');

function str_number_format($var, $digits)
{
    if (is_numeric($var))
        return number_format($var, $digits, ",", "");
    else
        return $var;
}

//Supporto per la colorazione alternata delle righe
$_flipflop = true;
function flipflop($flip=true, $flop=false)
{
    global $_flipflop;
    $_flipflop = !$_flipflop;
    if ($_flipflop)
        return $flip;
    else
        return $flop;
}

function get_days_array($date_start, $date_end)
{
    $start_date = date_create($date_start);
    $stop_date = date_create($date_end);
    $curr_date = date_create($date_start);
    $days = array();
    while ($curr_date < $stop_date)
    {
        array_push($days, date_create($curr_date->format(DATE_ATOM)));
        $curr_date->modify("+1 day");
    }
    return $days;
}

function float2str($float, $show_zero=true)
{
    return ($float or $show_zero) ? str_number_format($float, 2) : "";
}

function format_date($date)
{
    return $date->format("Y-m-d");
}

class timebalance extends atkNode
{
    var $m_user = "";

    function timebalance()
    {
        $this->atkNode("timebalance");
        $this->overtime_node = &atkGetNode("timereg.overtime_balance");
        $this->holidays_node = &atkGetNode("scheduler.scheduler_holidays");
        $this->weekreport_node = &atkGetNode("reports.weekreport");
        $this->m_user = $this->processGlobalVar("userid");
        atkdebug("DEBUG: m_user = ".$this->m_user);
        if($this->m_user=="" or (!$this->allowed("any_user"))) $this->m_user = $this->getInitialUser();
        $this->m_date_start = $this->processGlobalVar("date_start");
        $this->m_date_end = $this->processGlobalVar("date_end");
        $this->m_securityMap["bancaore"] = "add";
    }

    function getInitialUser() {
        global $g_user;
        return $g_user["id"];
    }

    function processGlobalVar($name, $value="")
    {
      $sessionManager = &atkGetSessionManager();
      $session = &$sessionManager->getSession();
      $sessionvarid = "timebalance_$name";
      if (empty($value) && isset($_REQUEST[$name])) $value = $_REQUEST[$name];
      if ($value!=="") $session[$sessionvarid] = $value;
      return $session[$sessionvarid];
    }

    function get_timesummary($userid)
    {
        $db = &atkGetDb();

        // Accetta una data in ingresso oppure imposta `date_start` all'inizio
        // dell'ultimo mese appena concluso
        $last_month_start = mktime(0, 0, 0, date("m")-1, 1, date("Y"));
        $date_start = $this->m_date_start;
        if ($date_start == "")
            $date_start = date("Y-m-d", $last_month_start);
        else if (is_array($date_start))
            $date_start = sprintf("%04d",$date_start["year"])."-".sprintf("%02d",$date_start["month"])."-".sprintf("%02d",$date_start["day"]);
        // Se non definito imposta `date_end` al primo giorno del mese
        // successivo a `date_start`
        $date_end = $this->m_date_end;
        $last_month_end = mktime(0, 0, 0, date("m"), 1, date("Y"));
        if ($date_end == "") {
            $date_end = date_create($date_start);
            $date_end->modify("+1 month");
            $date_end = format_date($date_end);
        } else if (is_array($date_end))
            $date_end = sprintf("%04d",$date_end["year"])."-".sprintf("%02d",$date_end["month"])."-".sprintf("%02d",$date_end["day"]);

        if ( $userid != 'all' )
            $single_user = "AND hours.userid ='$userid'";
        else
            $single_user = "";

        /* Rimossa la join con usercontract dato che era la causa del problema
        e i dati presenti nella tabella non erano utilizzati da nessuna parte
        nella query. */
        $ore_registrate = "
                SELECT person.id, person.userid, person.firstname, person.lastname,
                    hours.activitydate, hours.time/60 AS time_spent,
                    (project.name = '__dipendenti' AND phase.name = 'malattia') AS malattia,
                    (project.name = '__dipendenti' AND phase.name = 'ferie') AS ferie,
                    (project.name = '__dipendenti' AND phase.name = 'allattamento') AS allattamento,
                    (project.name = '__dipendenti' AND phase.name = 'donazioni_sangue') AS donazioni_sangue,
                    (project.name = '__dipendenti' AND LOWER(phase.name) LIKE 'permessi%') AS permessi_certificati,
                    (project.name LIKE 'd20_%') AS d20
                FROM hours
                    JOIN person ON hours.userid = person.id
                    JOIN phase ON hours.phaseid = phase.id
                    JOIN activity ON hours.activityid = activity.id
                    JOIN project ON phase.projectid = project.id
                WHERE activitydate >= '$date_start'
                    AND activitydate < '$date_end'
                    $single_user
        ";

        $select = "
SELECT id, userid, firstname, lastname, activitydate, SUM(time_worked) as time_worked,
       SUM(time_malattia) as time_malattia, SUM(time_ferie) as time_ferie, SUM(time_permessi_certificati) as time_permessi_certificati,
       SUM(time_allattamento) as time_allattamento, SUM(time_donazioni_sangue) as time_donazioni_sangue,
       SUM(time_d20) as time_d20, (SUM(time_worked) >= 5) as tickets
    FROM (
        SELECT id, userid, firstname, lastname, activitydate,
            (NOT (malattia OR ferie OR allattamento OR donazioni_sangue OR permessi_certificati OR d20)) * time_spent AS time_worked,
            malattia * time_spent AS time_malattia,
            ferie * time_spent AS time_ferie,
            allattamento * time_spent AS time_allattamento,
            donazioni_sangue * time_spent AS time_donazioni_sangue,
            permessi_certificati * time_spent AS time_permessi_certificati,
            d20 * time_spent AS time_d20
        FROM (
            $ore_registrate
        ) AS lavoro
    ) AS parts
GROUP BY id, userid, firstname, lastname, activitydate;
";
        $rows = $db->getRows($select);
        return array("date_start" => $date_start,
                     "date_end" => $date_end,
                     "rows" => $rows);
    }

    function get_hours_table($userid, &$timesummary=null, $export=false)
    {
        if (!$timesummary) {
            $timesummary = $this->get_timesummary($userid);
        }
        $content = array('<form action="dispatch.php" method="get" name="entryform">');
        $title = "TimeBalance: ".$timesummary["date_start"]." - ".$timesummary["date_end"]." (escluso)";

        if (!$export) {
            $this->addStyle("style.css");
            $content[] = session_form();

            $startdateatt = new atkDateAttribute("date_start", "F d Y", "d F Y",0 , 0, AF_OBLIGATORY);
            $enddateatt = new atkDateAttribute("date_end", "F d Y", "d F Y", 0, 0, AF_OBLIGATORY);

            $_startdate = array("date_start" => $startdateatt->dateArray($timesummary["date_start"]));
            $_enddate = array("date_end" => $enddateatt->dateArray($timesummary["date_end"]));

            $content[] = "Da ".$startdateatt->edit($_startdate)." a ".$enddateatt->edit($_enddate)."<br>\n";
            $content[] = '<input type="button" onclick="ATK.DateAttribute.modifyValue(\'date_start\', {month: \'+1\'}); ATK.DateAttribute.modifyValue(\'date_end\', {month: \'+1\'})" value="Mese+" />';
            $content[] = '<input type="button" onclick="ATK.DateAttribute.modifyValue(\'date_start\', {month: \'-1\'}); ATK.DateAttribute.modifyValue(\'date_end\', {month: \'-1\'})" value="Mese-" />';
            $weekreport = &atkGetNode("reports.weekreport");
            if ($this->allowed("any_user")) $content[] = 'User: <SELECT name="userid">'.$weekreport->get_employees($userid, $key="lastname").'</SELECT>';
            $content[] = '<input type="submit" value="'.atktext("refresh").'">';
            $content[] = '</form>';
            $content[] = "<h2>$title</h2>\n";
        }

        if ($userid!="all") {
            // Prepara dati in struttura 3d
            $daykeys = array_map("format_date", get_days_array($timesummary["date_start"], $timesummary["date_end"]));
            $workdays = $this->overtime_node->enumWorkingDays($timesummary["date_start"], $timesummary["date_end"], $userid, 'Y-m-d');
            atkdebug("workdays: ".var_export($workdays, 1));
            $alldays = array();
            foreach ($daykeys as $day) {
                $alldays[$day] = array('time_contract' => floatval($workdays[$day]),
                                       'time_worked' => 0.0,
                                       'time_malattia' => 0.0,
                                       'time_ferie' => 0.0,
                                       'time_permessi_certificati' => 0.0,
                                       'time_allattamento' => 0.0,
                                       'time_donazioni_sangue' => 0.0,
                                       'time_d20' => 0.0,
                                       'tickets' => 0,
                                       'lavoro' => false);
            }
            $time_balance = array();
            $userinfo = $this->weekreport_node->get_employee($userid);
            $time_balance[$userinfo['userid']] = array('id' => $userinfo['id'],
                                                       'fullname' => $userinfo['lastname']." ".$userinfo['firstname'],
                                                       'activity' => $alldays,
                                                       'tot_contract' => 0.0,
                                                       'tot_worked' => 0.0,
                                                       'tot_malattia' => 0.0,
                                                       'tot_ferie' => 0.0,
                                                       'tot_permessi_certificati' => 0.0,
                                                       'tot_allattamento' => 0.0,
                                                       'tot_donazioni_sangue' => 0.0,
                                                       'tot_d20' => 0.0,
                                                       'tot_tickets' => 0,
                                                      );
            foreach ($timesummary['rows'] as $key => $r)
            {
                $activitydate = array('time_worked' => floatval($r['time_worked']),
                                      'time_malattia' => floatval($r['time_malattia']),
                                      'time_ferie' => floatval($r['time_ferie']),
                                      'time_permessi_certificati' => floatval($r['time_permessi_certificati']),
                                      'time_allattamento' => floatval($r['time_allattamento']),
                                      'time_donazioni_sangue' => floatval($r['time_donazioni_sangue']),
                                      'time_d20' => floatval($r['time_d20']),
                                      'tickets' => intval($r['tickets']),
                                      'lavoro' => true);
                $activitydate = array_merge($time_balance[$r['userid']]['activity'][$r['activitydate']], $activitydate);

                $time_balance[$r['userid']]['activity'][$r['activitydate']] = $activitydate;
                $time_balance[$r['userid']]['tot_worked'] += $activitydate['time_worked'];
                $time_balance[$r['userid']]['tot_malattia'] += $activitydate['time_malattia'];
                $time_balance[$r['userid']]['tot_ferie'] += $activitydate['time_ferie'];
                $time_balance[$r['userid']]['tot_permessi_certificati'] += $activitydate['time_permessi_certificati'];
                $time_balance[$r['userid']]['tot_allattamento'] += $activitydate['time_allattamento'];
                $time_balance[$r['userid']]['tot_donazioni_sangue'] += $activitydate['time_donazioni_sangue'];
                $time_balance[$r['userid']]['tot_d20'] += $activitydate['time_d20'];
                $time_balance[$r['userid']]['tot_tickets'] += $activitydate['tickets'];

                $time_balance[$r['userid']]['activity'][$r['activitydate']]['totale_parziale_worked'] = $time_balance[$r['userid']]['tot_worked'];
                //atkdebug("TimeBalance loop: ".var_export($r, 1));
            }
            //atkdebug("TimeBalance: ".var_export($time_balance, 1));
            return array("content" => $content,
                         "time_balance" => $time_balance,
                         "title" => $title,
                         "timesummary" => &$timesummary);
        } else {
            return array("content" => $content,
                         "title" => $title,
                         "timesummary" => &$timesummary);
        }
    }
    function get_active_employees($date_start, $date_end)
    {
        $userid = $this->m_user;
        if ($userid=="all") {
            $db = &atkGetDb();
            $select = "
                SELECT DISTINCT usercontract.userid FROM usercontract
                    JOIN person ON usercontract.userid = person.id
                WHERE
                    NOT (enddate < '$date_start' OR '$date_end' < startdate)
                    AND description NOT LIKE '%NO PRESENZE%'
                ORDER BY ABS(person.initials)
            ";
            $users = array();
            foreach($db->getRows($select) as $r) {
                $users[] = $r['userid'];
            }
        } else {
            $users = array($userid);
        }
        return $users;
    }
    function action_report(&$handler, $userid=null, &$timesummary=null, $export=false)
    {
        if (!$userid) {
            $userid = $this->m_user;
        }
        if (!$timesummary) {
            $timesummary = $this->get_timesummary($userid);
        }
        $hours_table = $this->get_hours_table($userid, $timesummary, $export);
        $content = $hours_table['content'];
        $timesummary = $hours_table['timesummary'];

        /*
        Affinche' il conteggio degli straordinari funzioni correttamente
        e' necessario che:
         * il giorno in cui si calcola il credito/debito di ore all'inizio
         del range selezionato sia l'ultimo del mese precedente.
         * il giorno in cui si calcola il credito/debito di ore alla fine
         del bilancio sia l'ultimo del mese in corso.

        Pertanto i valori di date_start e date_end sono modificati per
        rispecchiare queste specifiche.

        NOTE: queste modifica ha come requisito l'eliminazione dei check
        inseriti da lmancini nella revisione r28891. Questo potrebbe
        portare a malfunzionamenti. Ergo, prima di inserire le modifiche
        sull'achievo in produzione sarebbe raccomandabile il testarlo. A
        fondo.
        */
        $date_start = date_create($timesummary["date_start"]);
        $date_start->modify("-1 day");
        $date_start = format_date($date_start);

        $date_end = date_create($timesummary["date_end"]);
        $date_end->modify("-1 day");
        $date_end = format_date($date_end);

        if ($userid!="all") {
            // Tabelle banca ore
            $banca_head = "<tr><th>User</th><th>Nome</th><th>Giorno</th><th>Ore previste</th>
                <th>Ore lavorate</th><th>Malattia</th><th>Ferie</th><th>Permessi Certificati</th>
                <th>Allattamento</th><th>Donazioni</th><th>Develer20</th><th>Tickets</th>
                               </tr>\n";
            foreach ($hours_table['time_balance'] as $iuserid => &$userdata)
            {
                $content[] = "<h3>$iuserid - ".$userdata['fullname']."</h3>\n";
                $content[] = "<table border='1'>\n";
                $content[] = $banca_head;

                $userdata['date_start']['balance'] = $this->overtime_node->getBalance($date_start, $userdata['id'], true);
                $userdata['date_end']['balance'] = $this->overtime_node->getBalance($date_end, $userdata['id']);

                $banca_ore = $userdata['date_start']['balance']['balance'] + $userdata['tot_worked'];

                foreach ($userdata['activity'] as $date => &$activity)
                {
                    $holiday = $this->holidays_node->isHoliday($date);
                    if ( $holiday ) {
                        $bgcolor = "#CCBBFF";
                    } else {
                        $bgcolor = flipflop("#9999FF", "#BBBBFF");
                    }
                    $abstime = strtotime($date);

                    $content[] = "<tr style='background: $bgcolor'>";
                    $content[] = "<td>$iuserid</td>";
                    $content[] = "<td>".$userdata['fullname']."</td>";
                    if ( $this->holidays_node->isHoliday($date) ) {
                        $content[] = "<td><b><i>".strftime("%Y-%m-%d %a", $abstime)."</i></b></td>";
                    } elseif ( !($activity['time_contract'] > 0) ) {
                        $content[] = "<td><strike>".strftime("%Y-%m-%d %a", $abstime)."</strike></td>";
                    } else {
                        $content[] = "<td>".strftime("%Y-%m-%d %a", $abstime)."</td>";
                    }
                    if ( $activity['lavoro'] or ($activity['time_contract'] > 0) ) {
                        $userdata['tot_contract'] += $activity['time_contract'];
                        $content[] = "<td align='right' title='".float2str($userdata['tot_contract'])."'>".float2str($activity['time_contract'])."</td>";
                        $banca_ore -= $activity['time_worked'];
                    } else {
                        $content[] = "<td align='right'>&nbsp</td>"; // time_contract
                    }
                    if ( $activity['lavoro'] or ($activity['time_contract'] > 0) )
                    {
                        $time_other = $activity['time_malattia'] + $activity['time_ferie'] + $activity['time_permessi_certificati'] +
                                      $activity['time_allattamento'] + $activity['time_donazioni_sangue'];
                        if ( $holiday and ($time_other>0) ) {$style="color: red";} else {$style="";}
                        $content[] = "<td align='right' style='$style' title='".float2str($activity['totale_parziale_worked'])."'>".float2str($activity['time_worked'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_malattia'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_ferie'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_permessi_certificati'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_allattamento'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_donazioni_sangue'])."</td>";
                        $content[] = "<td align='right' style='$style'>".float2str($activity['time_d20'])."</td>";
                        $content[] = "<td align='right' style='$style'>".$activity['tickets']."</td>";
                    }
                    else
                    {
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                        $content[] = "<td align='right'>&nbsp</td>";
                    }
                    $content[] = "</tr>\n";
                } // fine loop su tutti i giorni selezionati
                $content[] = $banca_head;
                $content[] = "<tr><td>$iuserid</td><td>".$userdata['fullname']."</td><th>Totale</th>";
                $content[] = "<th align='right'>".float2str($userdata['tot_contract'])."</th>";
                $overtime_balance = $userdata['date_start']['balance']['balance'] + $userdata['tot_worked'];
                $content[] = "<th align='right' title='banca ore: ".float2str($overtime_balance)."'>";
                    $content[] = float2str($userdata['tot_worked'])."</th>";
                if ($overtime_balance > 0) {
                    $userdata['overtime_balance'] = $overtime_balance;
                } else {
                    $userdata['overtime_balance'] = 0.0;
                }
                $content[] = "<th align='right'>".float2str($userdata['tot_malattia'])."</th>";
                $content[] = "<th align='right'>".float2str($userdata['tot_ferie'])."</th>";
                $content[] = "<th align='right'>".float2str($userdata['tot_permessi_certificati'])."</th>";
                $content[] = "<th align='right'>".float2str($userdata['tot_allattamento'])."</th>";
                $content[] = "<th align='right'>".float2str($userdata['tot_donazioni_sangue'])."</th>";
                $content[] = "<th align='right' title='".float2str($userdata['tot_d20'])."'>";
                    $content[] = float2str(min($userdata['tot_d20'], $userdata['tot_worked']*0.2))."</th>";
                $content[] = "<th align='right'>".$userdata['tot_tickets']."</th>";
                $content[] = "</tr>\n";
                $content[] = "<tr><td colspan='13'>$title<dl>\n";
                $content[] = "<b>$iuserid:</b> ".$userdata['fullname']."<br>";
                $content[] = "<b>Banca ore al ".$timesummary["date_start"].":</b> ".$this->overtime_node->getOvertimeLink($userdata['date_start'])."<br>";
                /*
                Aggiunto link per permettere di modificare il quantitativo di ore
                presenti in banca ore dopo aver stabilito con il dipendente quante
                di queste devono essere pagate come straordinari e quante messe da
                parte.
                */
                $content[] = "<b>Banca ore al ".$timesummary["date_end"].":</b> ".$this->overtime_node->getOvertimeLink($userdata['date_end'])."<br>";
                //atkdebug("userdata['overtime_balance']=".$userdata['overtime_balance']);
                //atkdebug("userdata['date_end']['balance']['balance']=".$userdata['date_end']['balance']['balance']);
                $userdata['straordinari'] = max($userdata['overtime_balance'] - max($userdata['date_end']['balance']['balance'], 0.0), 0.0);
                $content[] = "<b>Straordinari:</b> ".float2str($userdata['straordinari'])."<br>";
                $content[] = "<b>Ore Previste:</b> ".float2str($userdata['tot_contract'])."<br>";
                $content[] = "<b>Ore Lavorate:</b> ".float2str($userdata['tot_worked'])."<br>";
                $content[] = "<b>Malattia:</b> ".float2str($userdata['tot_malattia'])."<br>";
                $ore_mancanti = $userdata['tot_contract'] - $userdata['tot_worked'] - $userdata['tot_malattia'] - $userdata['tot_permessi_certificati'] - $userdata['tot_allattamento'] - $userdata['tot_donazioni_sangue'];
                $content[] = "<b>Ferie:</b> <a title='segnate: ".float2str($userdata['tot_ferie'])." mancanti: ".float2str($ore_mancanti)."'>".float2str($userdata['tot_ferie'] + $ore_mancanti)."</a><br>";
                $content[] = "<b>Permessi certificati:</b> ".float2str($userdata['tot_permessi_certificati'])."<br>";
                $content[] = "<b>Allattamento:</b> ".float2str($userdata['tot_allattamento'])."<br>";
                $content[] = "<b>Donazioni sangue:</b> ".float2str($userdata['tot_donazioni_sangue'])."<br>";
                $content[] = "<b>Develer20:</b> <a title='".float2str($userdata['tot_d20'])."'>";
                    $content[] = float2str(min($userdata['tot_d20'], $userdata['tot_worked']*0.2))."</a><br>";
                $content[] = "<b>Tickets:</b> ".$userdata['tot_tickets']."<br>";

                $lock_type = Locking::getLockType($userdata['id'], $date_end);
                if ($lock_type === "")
                {
                    $content[] = '<div class="atkmessages_general"><form action="dispatch.php" method="get" name="entryform">';
                    if ($userdata['date_end']['balance']['balance'] > 0)
                    {
                        $content[] = 'In banca ore ci sono '.$userdata['date_end']['balance']['balance'].' ore';
                        $content[] = '<button name="option" type="submit" value="store">'.atktext("Conferma mese e lascia tutto in banca ore").'</button>';
                        $content[] = '<button name="option" type="submit" value="empty">'.atktext("Conferma mese e riscatta tutte le ore").'</button>';
                    } else if ($userdata['date_end']['balance']['balance'] < 0) {
                        $content[] = 'Banca ore in negativo, le ore mancanti verranno prese dalle ferie';
                        $content[] = '<button name="option" type="submit" value="empty">'.atktext("Conferma mese").'</button>';
                    } else {
                        $content[] = 'Banca ore in pari';
                        $content[] = '<button name="option" type="submit" value="empty">'.atktext("Conferma mese").'</button>';
                    }
                    $content[] = '<input type="hidden" name="userid" value="'.$userdata['id'].'">';
                    $content[] = '<input type="hidden" name="balance_date" value="'.$date_end.'">';
                    $content[] = '<input type="hidden" name="atknodetype" value="'.$this->atkNodeType().'">';
                    $content[] = '<input type="hidden" name="atkaction" value="bancaore">';
                    $content[] = '</form></div>';
                } else {
                    $content[] = "<div class='atkmessages_success'>Mese confermato, chiedere ad un admin di sbloccarlo se ci sono errori da correggere.</div>";
                }

                $content[] = "</dl></td></tr>\n";
                $content[] = "</table>\n";
            }
        } else {
            $content[] = "<p><i>Sono stati selezionati tutti gli utenti, e' possibile solo scaricare il foglio di calcolo.</i></p>";
            $users = $this->get_active_employees($timesummary["date_start"], $timesummary["date_end"]);
            $content[] = "<ul>";
            foreach($users as $iuserid) {
                $lock = !(Locking::getLockType($iuserid, $date_end) === "");
                $message = $lock ? "Bloccate" : "da bloccare";
                $style = $lock ? "" : "font-weight: bold";
                $userinfo = $this->weekreport_node->get_employee($iuserid);
                if ($userinfo['id'] != $iuserid)
                    continue;
                $content[] = "<li><span style='$style'>" . $userinfo['lastname']." ".$userinfo['firstname'] . "</span> - $message</li>";
            }
            $content[] = "</ul>";
        }

        $content[] = "<p>Pagina creata il ".date_create()->format("Y-m-d")."</p>\n";

        $content[] = '<form action="dispatch.php" method="get" name="entryform">';
        $content[] = '<input type="submit" value="'.atktext("Esporta").'">';
        $content[] = '<input type="hidden" name="date_start" value="'.$timesummary["date_start"].'">';
        $content[] = '<input type="hidden" name="atknodetype" value="'.$this->atkNodeType().'">';
        $content[] = '<input type="hidden" name="atkaction" value="export">';
        $content[] = '</form>';

        if (!$export) {
            $page = &$this->getPage();
            $ui = &$this->getUi();
            $page->addContent($ui->renderBox(array("title"=>$hours_table['title'], "content"=>implode('', $content))));
        } else {
            unset($content);
            return $hours_table;
        }

    }
    function action_export(&$handler)
    {
        include "excel_xml.php";
        $userid = $this->m_user;

        $exporter = &atknew("atk.utils.atkfileexport");
        $timesummary = $this->get_timesummary($userid);
        $report_time = strtotime($timesummary["date_start"]);

        $date_start = $timesummary["date_start"];
        $date_end = $timesummary["date_end"];

        $users = $this->get_active_employees($date_start, $date_end);

        $excel = new excel_xml();
        $title_style = array(
            'bold'       => 1,
            'size'       => '14'
        );
        $header_style = array(
            'bold'       => 1,
        );
        $excel->add_style('title', $title_style);
        $excel->add_style('header', $header_style);
        $excel->add_row(explode(";", ";DEVELER SRL Registro presenze Anno ".strftime("%Y", $report_time)." mese di ".strftime("%B", $report_time)."|30"), 'title');
        $header = true;
        function fformat($number) {
            return number_format($number, 2, $dec_point='.', $thousands_sep='');
        }
        function coalesce($value) {
            return "IF($value, ($value), \"\")";
        }
        function if_working($time_contract, $value) {
            return "IF(R[-1]C=$time_contract, ($value), \"\")";
        }
        $buoni_pasto = array();
        foreach ($users as $iuserid) {
            $hours_table = $this->action_report($handler, $iuserid, $timesummary, true);
            foreach ($hours_table['time_balance'] as $username => $userdata)
            {
                if ($userdata['id'] != $iuserid)
                    continue;

                if ($header) {
                    $totali = ";Totali;";
                }

                $buoni_pasto[$userdata['fullname']] = $userdata['tot_tickets'];

                $ore_ordinarie = "";
                $straordinarie = "";
                $permessi_certificati = "";
                $allattamento = "";
                $donazioni_sangue = "";
                $malattia = "";

                $sum_rows = 2;
                if ($userdata['tot_allattamento'] > 0)
                    $sum_rows++;
                if ($userdata['tot_permessi_certificati'] > 0)
                    $sum_rows++;
                if ($userdata['tot_donazioni_sangue'] > 0)
                    $sum_rows++;

                $counter = -1;
                foreach ($userdata['activity'] as $date => $activity)
                {
                    $counter++;
                    if ($header) {
                        $abstime = strtotime($date);
                        $totali .= strftime(";%d %a", $abstime);
                    }
                    $value = fformat($activity['time_worked']);
                    $ore_ordinarie .= ";-1=".coalesce(fformat($activity['time_worked']));
                    $permessi_certificati .= ";-1=".coalesce(fformat($activity['time_permessi_certificati']));
                    $allattamento .= ";-1=".coalesce(fformat($activity['time_allattamento']));
                    $donazioni_sangue .= ";-1=".coalesce(fformat($activity['time_donazioni_sangue']));
                    $malattia .= ";-1=".coalesce(fformat($activity['time_malattia']));
                }
                if ($header) {
                    $excel->add_row(explode(";", $totali), 'header');
                    $header = false;
                } else {
                    $excel->add_row("");
                }

                $head_ore_ordinarie = "*".$userdata['fullname']."*;-1=SUM(RC[2]:RC[33]);Ore ordinarie";
                $head_malattia = fformat($userdata['tot_contract']).";-1=SUM(RC[2]:RC[33]);Malattia";
                $head_permessi_certificati = ";-1=SUM(RC[2]:RC[33]);Permessi Certificati";
                $head_allattamento = ";-1=SUM(RC[2]:RC[33]);Allattamento";
                $head_donazioni_sangue = ";-1=SUM(RC[2]:RC[33]);Donazioni sangue";

                $excel->add_row(explode(";", $head_ore_ordinarie.$ore_ordinarie));
                $excel->add_row(explode(";", $head_malattia.$malattia));
                if ($userdata['tot_allattamento'] > 0)
                    $excel->add_row(explode(";", $head_allattamento.$allattamento));
                if ($userdata['tot_permessi_certificati'] > 0)
                    $excel->add_row(explode(";", $head_permessi_certificati.$permessi_certificati));
                if ($userdata['tot_donazioni_sangue'] > 0)
                    $excel->add_row(explode(";", $head_donazioni_sangue.$donazioni_sangue));
            }
        }

        $excel->add_row("");
        $excel->add_row("*Buoni Pasto*");
        ksort($buoni_pasto);
        foreach ($buoni_pasto as $fullname => $buoni_pasto_user) {
            $excel->add_row(array($fullname, $buoni_pasto_user));
        }

        $excel->create_worksheet('Develer srl');
        $xml = $excel->generate();
        if ($userid == "all")
            $exporter->export($xml, strftime("Develer-%Y-%B", $report_time), "xls");
        else
            $exporter->export($xml, strftime("Develer-%Y-%B-$username", $report_time), "xls");
    }
    function action_bancaore(&$handler)
    {
        $userid = $this->m_postvars['userid'];
        $option = $this->m_postvars['option'];
        $balance_date = $this->m_postvars['balance_date'];
        if ($option == 'store') {
            $balance_record = $this->overtime_node->getBalance($balance_date, $userid);
            $balance = max(0.0, $balance_record['balance']);
        } else if ($option == 'empty') {
            $balance = 0.0;
        } else {
            atkdebug("action_bancaore: option=$option non supportata");
            return;
        }
        atkdebug("action_bancaore: $userid $option $balance_date $balance");
        $this->overtime_node->updateOvertimeBalance($userid, $balance_date, $balance, $manual=1);
        Locking::lockPeriod(Locking::getPeriod($balance_date), $approved=null, $userid=$userid);
        $this->redirect(dispatch_url("internal.timebalance", "report"));
    }
}
?>
