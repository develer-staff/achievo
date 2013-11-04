<?php
/*
 * Copyright 2007 Develer S.r.l. (http://www.develer.com/)
 * All rights reserved.
 *
 * $Id: $
 * Author: Matteo Bertini <naufraghi@develer.com>
 */

useattrib("atkdateattribute");
atkimport("module.timereg.locking");


function get_IVA($date)
{
  if ($date >= "2013-10-01")
    return 0.22;
  elseif ($date >= "2011-09-17")
    return 0.21;
  else
    return 0.2;
}

function str_number_format($var, $digits)
{
    if (is_numeric($var))
        return number_format($var, $digits, ",", ".");
    else
        return $var;
}

function startswith($text, $prefix)
{
    return strpos($text, $prefix) === 0;
}

//Supporto per la colorazione alternata delle righe
$_flipflop = true;
function flipflop($step=true)
{
    global $_flipflop;
    if ($step) {
      $_flipflop = !$_flipflop;
    }
    return $_flipflop;
}

function project_group($project_name)
{
  $prefixes = array("prj_", "d20_", "ext_", "__");
  foreach ($prefixes as $gkey)
  {
    if (startswith($project_name, $gkey)) return "PREFIX(".$gkey.")";
  }
  return "other";
}

class timesummary extends atkNode
{
    function timesummary()
    {
        $this->atkNode("timesummary");
    }

    function get_timesummary()
    {
        global $g_user;
        $userid = $g_user["id"];

        $db = &atkGetDb();

        /* Accetta una data in ingresso oppure imposta l'intervallo all'ultimo mese appena concluso*/
        $last_month_start  = mktime(0, 0, 0, date("m")-1, 1, date("Y"));
        $this_month_start = mktime(0, 0, 0, date("m"), 1, date("Y"));
        $date_start = $this->m_postvars['date_start'];
        $date_end = $this->m_postvars['date_end'];
        if ($date_start == "")
            $date_start = date("Y-m-d", $last_month_start);
        else
            $date_start = sprintf("%04d",$date_start["year"])."-".sprintf("%02d",$date_start["month"])."-".sprintf("%02d",$date_start["day"]);
        if ($date_end == "")
            $date_end = date("Y-m-d", $this_month_start);
        else
            $date_end = sprintf("%04d",$date_end["year"])."-".sprintf("%02d",$date_end["month"])."-".sprintf("%02d",$date_end["day"]);

        /* Accetta un testo con due colonne:
            username euro/ora
        */
        $euro_ora_text = $this->m_postvars['euro_ora_text'];
        $euro_ora_array = array();
        foreach (explode("\n", $euro_ora_text) as $line)
        {
            $line = explode(" ", $line);
            $euro_ora_array[$line[0]] = (real) $line[1];
        }
        atkdebug("euro_ora_array: ".var_export($euro_ora_array, 1));

        $select = "
                SELECT
                    person.id,
                    person.userid,
                    person.firstname, person.lastname,
                    project.name AS prj,
                    sum(hours.time)/60.0 as ore,
                    max(entrydate) as lastmod,
                    (phase.name != 'non billable') as billable
                FROM hours
                    JOIN person ON hours.userid = person.id
                    JOIN phase ON hours.phaseid = phase.id
                    JOIN activity ON hours.activityid = activity.id
                    JOIN project ON phase.projectid = project.id";
        $filter = "
                WHERE
                    activitydate >= '$date_start' AND
                    activitydate < '$date_end'";
        $filter_nopay = "
                AND NOT (
                    project.name = '__dipendenti' AND
                    phase.name IN ('ferie', 'malattia')
                )";
        if (!$this->allowed("any_user")) $filter.= " AND hours.userid ='$userid' ";
        $groupby = "
                GROUP BY lower(person.userid), lower(prj), billable
                ORDER BY lower(person.lastname), lower(person.firstname),
                         lower(person.userid), lower(prj), billable";

        $sql = $select . $filter . $filter_nopay . $groupby;

        return array("date_start" => $date_start,
                     "date_end" => $date_end,
                     "euro_ora_array" => $euro_ora_array,
                     "rows" => $db->getRows($sql));
    }

    function action_search()
    {
        atkdebug("timesummary Action search");

        $results = $this->get_timesummary();
        echo make_xml("timesummary", "$date_start<=D<$date_end", $results['rows'], $bodykey="remark");
        exit;
    }

    function action_report()
    {
        $ui = &$this->getUi();
        $page = &$this->getPage();
        $this->addStyle("style.css");
        $content = '<form action="dispatch.php" method="get" name="entryform">';
        $content .= session_form();

        $results = $this->get_timesummary();
        $title = "TimeSummary: ".$results["date_start"]." - ".$results["date_end"]." (escluso)";
        $IVA = get_IVA($results["date_end"]);

        $startdateatt = new atkDateAttribute("date_start", "F d Y", "d F Y",0 , 0, AF_OBLIGATORY);
        $enddateatt = new atkDateAttribute("date_end", "F d Y", "d F Y", 0, 0, AF_OBLIGATORY);

        $_startdate = array("date_start" => $startdateatt->dateArray($results["date_start"]));
        $_enddate = array("date_end" => $enddateatt->dateArray($results["date_end"]));

        $content .= "Da ".$startdateatt->edit($_startdate)." a ".$enddateatt->edit($_enddate);
        $content .= '<input type="button" onclick="ATK.DateAttribute.modifyValue(\'date_start\', {month: \'+1\'}); ATK.DateAttribute.modifyValue(\'date_end\', {month: \'+1\'})" value="Mese+" />';
        $content .= '<input type="button" onclick="ATK.DateAttribute.modifyValue(\'date_start\', {month: \'-1\'}); ATK.DateAttribute.modifyValue(\'date_end\', {month: \'-1\'})" value="Mese-" />';
        $content .= '<input type="submit" value="'.atktext("refresh").'">';
        $content .= "<h2>$title</h2>\n";
        $content .= "<p>Le ore segnate come malattia/ferie non vengono conteggiate.</p>\n";

        // Tabelle persone
        $content .= "<table><tr><td>";
            // Tabella ore lavorate per progetto
            $content .= "<table border='1'>\n";
            $content .= "<tr><th>Persona</th><th>Progetto</th><th>Ore</th><th>Euro</th></tr>\n";
            $progetti = array();
            $gprogetti = array();
            $persone = array();
            $userid_progetti = array();
            foreach ($results['rows'] as $key => $r)
            {
                if (!array_key_exists($r['userid'].$r['prj'], $userid_progetti)) {
                    $userid_progetti[$r['userid'].$r['prj']] = array('userid' => $r['userid'], 'prj' => $r['prj'], 'ore' => 0.0, 'euro' => 0.0);
                }
                if (!array_key_exists($r['prj'], $progetti)) {
                    $progetti[$r['prj']] = array("ore" => 0.0, "euro" => 0.0, "non-billable" => 0.0);
                }
                $r['gprj'] = project_group($r['prj']);
                if (!array_key_exists($r['gprj'], $gprogetti)) {
                    $gprogetti[$r['gprj']] = array("ore" => 0.0, "euro" => 0.0);
                }
                if (!array_key_exists($r['userid'], $persone)) {
                    $persone[$r['userid']] = array('id' => $r['id'], "ore" => 0.0, "euro" => 0.0, "euro_ora" => 0.0, "lastmod" => "0000-00-00");
                }
                $progetti[$r['prj']]['ore'] += $r['ore'];
                if (!$r['billable']) {
                    $progetti[$r['prj']]['non-billable'] += $r['ore'];
                }
                $progetti[$r['prj']]['lastmod'] = max($progetti[$r['prj']]['lastmod'], $r['lastmod']);
                $userid_progetti[$r['userid'].$r['prj']]['ore'] += $r['ore'];
                $gprogetti[$r['gprj']]['ore'] += $r['ore'];
                $gprogetti[$r['gprj']]['lastmod'] = max($gprogetti[$r['gprj']]['lastmod'], $r['lastmod']);
                $persone[$r['userid']]['ore'] += $r['ore'];
                $persone[$r['userid']]['lastmod'] = max($persone[$r['userid']]['lastmod'], $r['lastmod']);
                $persone[$r['userid']]['fullname'] = $r['lastname']." ".$r['firstname'];

                atkdebug("Euro ora loop: ".var_export($r, 1)." incr ore prj:".var_export($progetti[$r['prj']], 1).", user:".var_export($persone[$r['userid']], 1));

                if (array_key_exists($r['userid'], $results['euro_ora_array'])) {
                    $r['euro'] = $r['ore'] * $results['euro_ora_array'][$r['userid']];
                    $persone[$r['userid']]['euro_ora'] = $results['euro_ora_array'][$r['userid']];
                } else {
                    $r['euro'] = "Manca euro/h";
                    $persone[$r['userid']]['euro_ora'] = "Manca euro/h";
                }

                if (is_numeric($userid_progetti[$r['userid'].$r['prj']]['euro']) and is_numeric($r['euro'])) {
                    $userid_progetti[$r['userid'].$r['prj']]['euro'] += $r['euro'];
                } else {
                    $userid_progetti[$r['userid'].$r['prj']]['euro'] = "Manca euro/h";
                }
                if (is_numeric($progetti[$r['prj']]['euro']) and is_numeric($r['euro'])) {
                    $progetti[$r['prj']]['euro'] += $r['euro'];
                } else {
                    $progetti[$r['prj']]['euro'] = "Manca euro/h";
                }
                if (is_numeric($gprogetti[$r['gprj']]['euro']) and is_numeric($r['euro'])) {
                    $gprogetti[$r['gprj']]['euro'] += $r['euro'];
                } else {
                    $gprogetti[$r['gprj']]['euro'] = "Manca euro/h";
                }
                if (is_numeric($persone[$r['userid']]['euro']) and is_numeric($r['euro'])) {
                    $persone[$r['userid']]['euro'] += $r['euro'];
                } else {
                    $persone[$r['userid']]['euro'] = "Manca euro/h";
                }
            }
            $_last_user = "";
            foreach($userid_progetti as $key => $r)
            {
                if ($_last_user != $r['userid']) {
                  $_last_user = $r['userid'];
                  flipflop();
                }
                if (flipflop(false)) {
                    $content .= "<tr>";
                } else {
                    $content .= "<tr style='background: #DDDDC0'>";
                }
                $content .= "<td title='".$r['userid']."'>".$persone[$r['userid']]['fullname']."</td>";
                $content .= "<td>".$r['prj']."</td>";
                $content .= "<td align='right'>".str_number_format($r['ore'], 2)."</td>";
                $content .= "<td align='right'>".str_number_format($r['euro'], 2)."</td>";
                $content .= "</tr>\n";
            }
            $content .= "</table>";
        $content .= "</td><td>";
            // Tabella ore per persona
            $content .= "<table border='1'><tr><th>Totale persona</th><th>ore</th><th>euro/ora</th>";
            $content .= "<th>euro</th><th>IVA (".str_number_format(100*$IVA, 0)."%)</th><th>Ivato</th><th>Modificato</th><th>Bloccato</th></tr>";
            $totale_ore_develer = 0;
            foreach ($persone as $key => $pers)
            {
                if (flipflop())
                    $content .= "<tr>";
                else
                    $content .= "<tr style='background: #DDDDC0'>";
                $content .= "<th title='$key'>".$pers['fullname']."</th>";
                $content .= "<td align='right'>".str_number_format($pers['ore'], 2)."</td>";           // ore
                $content .= "<td align='right'>".str_number_format($pers['euro_ora'], 2)."</td>";      // euro/ora
                $content .= "<td align='right'>".str_number_format($pers['euro'], 2)."</td>";          // euro
                $content .= "<td align='right'>".str_number_format($pers['euro']*$IVA, 2)."</td>";     // IVA
                $content .= "<td align='right'>".str_number_format($pers['euro']*(1+$IVA), 2)."</td>"; // ivato
                $content .= "<td>".$pers['lastmod']."</td>";
                $date_lock = date_create($results["date_end"]);
                $date_lock->modify("-1 day");
                $lock = !(Locking::getLockType($pers['id'], $date_lock->format("Y-m-d")) === "") ? "OK" : "-";
                $content .= "<td>$lock</td>";
                $content .= "</tr>\n";
                $totale_ore_develer += $pers['ore'];
            }
            $content .= "<th>Totale</th><td align='right'>".str_number_format($totale_ore_develer, 2)."</td>";
            $content .= "<td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td></tr>\n";
            $content .= "</table>";
        $content .= "</td></tr></table>";

        // Tabelle progetti
        $content .= "<table><tr><td>";
            // Tabella ore per progetto
            $content .= "<table border='1'><tr><th>Totale progetto</th><th>Ore (b)</th><th>Ore (nb)</th><th>Ore (tot)</th><th>Euro</th><th>Modificato</th></tr>";
            uksort($progetti, "strcasecmp"); //Ordina per chiave, le persone sono già ordinate
            foreach ($progetti as $key => $prj)
            {
                if (flipflop())
                    $content .= "<tr>";
                else
                    $content .= "<tr style='background: #DDDDC0'>";
                $content .= "<th>$key</th>"; // Nome progetto
                $content .= "<td align='right'>".str_number_format($prj['ore'] - $prj['non-billable'], 2)."</td>";
                $content .= "<td align='right'>".str_number_format($prj['non-billable'], 2)."</td>";
                $content .= "<td align='right'>".str_number_format($prj['ore'], 2)."</td>";
                $content .= "<td align='right'>".str_number_format($prj['euro'], 2)."</td>";
                $content .= "<td>".$prj['lastmod']."</td>";
                $content .= "</tr>\n";
            }
            $content .= "</table>";
        $content .= "</td><td>";
            // Tabella ore per gruppo progetti
            $content .= "<table border='1'><tr><th>Totale gruppo progetti</th><th>Ore</th><th>Euro</th><th>Modificato</th></tr>";
            uksort($gprogetti, "strcmp");
            foreach ($gprogetti as $key => $prj)
            {
                if (flipflop())
                    $content .= "<tr>";
                else
                    $content .= "<tr style='background: #DDDDC0'>";
                $content .= "<th>$key</th><td align='right'>".str_number_format($prj['ore'], 2)."</td>";
                $content .= "<td align='right'>".str_number_format($prj['euro'], 2)."</td>";
                $content .= "<td>".$prj['lastmod']."</td>";
                $content .= "</tr>\n";
            }
            $content .= "</table>";
        $content .= "</td></tr></table>";

        $content .= '<input type="submit" value="'.atktext("refresh").'">';

        $content .= "<table><tr><td>Inserire: nome_utente euro/h</td></tr>\n";
        $content .= "<tr><td><textarea rows='5' cols='40' name='euro_ora_text'>".$this->m_postvars['euro_ora_text']."</textarea>";
        $content .= "</td></tr></table>\n";

        $content .= '<input type="submit" value="'.atktext("refresh").'">';
        $content .= '<input type="hidden" name="atknodetype" value="'.$this->atkNodeType().'">';
        $content .= '<input type="hidden" name="atkaction" value="report">';

        $content .= '</form>';
        $page->addContent($ui->renderBox(array("title"=>$title,"content"=>$content)));
    }
}
?>
